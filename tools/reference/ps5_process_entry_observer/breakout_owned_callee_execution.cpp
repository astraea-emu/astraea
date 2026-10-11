#include "breakout_owned_callee_execution.hpp"
#include "source_x86_64_decoder.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include <Zydis/Zydis.h>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::research {
namespace {
namespace l = astraea::loader;
namespace m = astraea::memory;
namespace e = astraea::execution;

constexpr std::uint64_t kCF = 1ULL << 0U;
constexpr std::uint64_t kPF = 1ULL << 2U;
constexpr std::uint64_t kAF = 1ULL << 4U;
constexpr std::uint64_t kZF = 1ULL << 6U;
constexpr std::uint64_t kSF = 1ULL << 7U;
constexpr std::uint64_t kOF = 1ULL << 11U;
constexpr std::uint64_t kArithmeticFlags = kCF | kPF | kAF | kZF | kSF | kOF;

[[nodiscard]] std::uint64_t* register_slot(
    e::GuestCpuContext& cpu, ZydisRegister reg) noexcept {
    switch (reg) {
    case ZYDIS_REGISTER_RAX: return &cpu.rax;
    case ZYDIS_REGISTER_RBX: return &cpu.rbx;
    case ZYDIS_REGISTER_RCX: return &cpu.rcx;
    case ZYDIS_REGISTER_RDX: return &cpu.rdx;
    case ZYDIS_REGISTER_RSI: return &cpu.rsi;
    case ZYDIS_REGISTER_RDI: return &cpu.rdi;
    case ZYDIS_REGISTER_RBP: return &cpu.rbp;
    case ZYDIS_REGISTER_RSP: return &cpu.rsp;
    case ZYDIS_REGISTER_R8: return &cpu.r8;
    case ZYDIS_REGISTER_R9: return &cpu.r9;
    case ZYDIS_REGISTER_R10: return &cpu.r10;
    case ZYDIS_REGISTER_R11: return &cpu.r11;
    case ZYDIS_REGISTER_R12: return &cpu.r12;
    case ZYDIS_REGISTER_R13: return &cpu.r13;
    case ZYDIS_REGISTER_R14: return &cpu.r14;
    case ZYDIS_REGISTER_R15: return &cpu.r15;
    default: return nullptr;
    }
}

[[nodiscard]] bool address_plus_signed(
    std::uint64_t base, std::int64_t displacement,
    std::uint64_t& output) noexcept {
    if (displacement >= 0) {
        const auto delta = static_cast<std::uint64_t>(displacement);
        if (base > std::numeric_limits<std::uint64_t>::max() - delta)
            return false;
        output = base + delta;
    } else {
        const auto magnitude = static_cast<std::uint64_t>(
            -(displacement + 1)) + 1U;
        if (base < magnitude) return false;
        output = base - magnitude;
    }
    return true;
}

[[nodiscard]] bool guest_stack_write(
    OwnedCalleeEvidence& state,
    std::uint64_t guest_va, std::uint64_t word) noexcept {
    if (guest_va < state.guest_stack_base) return false;
    const auto offset = guest_va - state.guest_stack_base;
    if (offset > state.guest_stack_bytes.size() - 8U) return false;
    for (std::size_t b = 0U; b < 8U; ++b)
        state.guest_stack_bytes[static_cast<std::size_t>(offset) + b] =
            static_cast<std::byte>((word >> (b * 8U)) & 0xffU);
    ++state.guest_stack_writes;
    return true;
}

[[nodiscard]] std::uint64_t subtract_flags(
    std::uint64_t lhs, std::uint64_t rhs,
    std::uint64_t result) noexcept {
    const bool parity = (std::popcount(
        static_cast<unsigned>(result & 0xffU)) % 2) == 0;
    return (lhs < rhs ? kCF : 0U) |
        (parity ? kPF : 0U) |
        (((lhs ^ rhs ^ result) & 0x10U) != 0U ? kAF : 0U) |
        (result == 0U ? kZF : 0U) |
        ((result >> 63U) != 0U ? kSF : 0U) |
        (((lhs ^ rhs) & (lhs ^ result) & (1ULL << 63U)) != 0U ?
             kOF : 0U);
}

[[nodiscard]] bool memory_base_address(
    e::GuestCpuContext& cpu,
    const ZydisDecodedOperand& operand,
    std::uint64_t next_rip,
    std::uint64_t& guest_address) noexcept {
    if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY ||
        operand.mem.index != ZYDIS_REGISTER_NONE ||
        (operand.mem.scale != 0U && operand.mem.scale != 1U))
        return false;
    std::uint64_t base = 0U;
    if (operand.mem.base == ZYDIS_REGISTER_RIP) {
        base = next_rip;
    } else {
        auto* reg = register_slot(cpu, operand.mem.base);
        if (reg == nullptr) return false;
        base = *reg;
    }
    return address_plus_signed(
        base,
        operand.mem.disp.has_displacement ? operand.mem.disp.value : 0,
        guest_address);
}

// Only the source-backed instruction is decoded. The runtime does NOT
// inspect a native host instruction pointer or enter guest machine code.
[[nodiscard]] bool decode_record(
    ZydisDecoder& decoder, const DecodedSourceInstruction& record,
    ZydisDecodedInstruction& instruction,
    std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT>& ops) {
    if (record.length == 0U || record.length > record.bytes.size() ||
        ZYAN_FAILED(ZydisDecoderDecodeFull(
            &decoder, record.bytes.data(), record.length,
            &instruction, ops.data())))
        return false;
    return instruction.length == record.length &&
        instruction.mnemonic == static_cast<ZydisMnemonic>(record.mnemonic);
}

[[nodiscard]] bool write_stack_operand(
    OwnedCalleeEvidence& state,
    const ZydisDecodedOperand& mem,
    std::uint64_t next_rip, std::uint64_t word) {
    if (mem.size != 64U) return false;
    std::uint64_t address = 0U;
    if (!memory_base_address(state.context, mem, next_rip, address))
        return false;
    return guest_stack_write(state, address, word);
}

[[nodiscard]] bool import_stop(
    const l::GuestImage& image, const ZydisDecodedInstruction& instruction,
    const ZydisDecodedOperand& mem,
    std::uint64_t call_rip, OwnedCalleeEvidence& state) {
    if (instruction.mnemonic != ZYDIS_MNEMONIC_CALL ||
        instruction.operand_count_visible != 1U ||
        mem.type != ZYDIS_OPERAND_TYPE_MEMORY || mem.size != 64U ||
        mem.mem.base != ZYDIS_REGISTER_RIP ||
        mem.mem.index != ZYDIS_REGISTER_NONE ||
        !mem.mem.disp.has_displacement ||
        !image.general_relocations.rela.has_value() ||
        !image.dynamic_symbols.has_value() ||
        !image.dynamic_strings.has_value() ||
        !image.dynamic_strings->string_table.has_value())
        return false;

    std::uint64_t slot = 0U;
    if (call_rip > std::numeric_limits<std::uint64_t>::max() -
                       instruction.length ||
        !memory_base_address(state.context, mem, call_rip +
            instruction.length, slot))
        return false;
    // Demand a unique verified external import relocation for the actual
    // RIP-relative GOT operand; do NOT load a GOT pointer or follow it.
    const auto view = image.initialized_image_view();
    if (!view.has_value()) return false;
    std::size_t matches = 0U;
    std::uint32_t symbol_index = 0U;
    const auto count = image.general_relocations.rela->count;
    for (std::uint64_t i = 0U; i < count; ++i) {
        auto record = l::parse_dynamic_relocation(
            image.general_relocations.rela.value(), i,
            image.dynamic_symbols.value(), view.value());
        if (!record.has_value()) return false;
        if (record->target.value() != slot) continue;
        if (record->relocation_type != 6U ||
            record->symbol_index == 0U) return false;
        ++matches;
        symbol_index = record->symbol_index;
    }
    if (matches != 1U) return false;
    const auto symbol = l::materialize_sce_dynamic_symbol(
        image.dynamic_symbols.value(),
        image.dynamic_strings->string_table.value(),
        symbol_index, view.value());
    if (!symbol.has_value() || symbol->symbol.section_index_raw != 0U ||
        symbol->symbol.type() != 2U ||
        symbol->name.raw.size() != 11U ||
        symbol->name.identity.has_value())
        return false;
    state.stopped_call_rip = call_rip;
    state.unresolved_got_slot = slot;
    state.unresolved_symbol_index = symbol_index;
    state.unresolved_bare_nid = symbol->name.raw;
    return true;
}
} // namespace

OwnedCalleeResult run_owned_breakout_callee(
    const l::GuestImage& image, const BreakoutEntryStepEvidence& entry) {
    const auto fail = [](OwnedCalleeError e) {
        return OwnedCalleeResult::failure(e);
    };
    if (entry.context.rip != entry.branch_target ||
        entry.context.rsp != entry.guest_stack_write_address ||
        entry.context.rsp > std::numeric_limits<std::uint64_t>::max() - 7U ||
        image.initial_stack.storage.size().value() != 4096U)
        return fail(OwnedCalleeError::unexpected_context);

    const auto decoded = decode_source_x86_64(
        image, entry.context.rip, kC1MaxDecodedInstructions);
    if (!decoded.has_value() || decoded->count != kC1MaxDecodedInstructions)
        return fail(OwnedCalleeError::decode_refused);
    OwnedCalleeEvidence state{};
    state.context = entry.context;
    state.initial_callee_rsp = entry.context.rsp;
    state.guest_stack_base = image.initial_stack.storage.base().value();

    // Recreate exactly the previous owned eight-byte entry CALL return push.
    // Every subsequent write targets this independent simulation buffer.
    std::uint64_t return_word = 0U;
    for (std::size_t b = 0U; b < 8U; ++b)
        return_word |= std::uint64_t{
            std::to_integer<unsigned>(entry.guest_stack_return_word[b])}
            << (b * 8U);
    if (!guest_stack_write(state, entry.guest_stack_write_address,
                           return_word))
        return fail(OwnedCalleeError::stack_out_of_bounds);
    state.guest_stack_writes = 0U; // entry instruction already accounted for

    ZydisDecoder decoder{};
    if (ZYAN_FAILED(ZydisDecoderInit(
            &decoder, ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64)))
        return fail(OwnedCalleeError::decode_refused);
    for (const auto& record : decoded->instructions) {
        if (record.rip != state.context.rip)
            return fail(OwnedCalleeError::unsupported_instruction);
        ZydisDecodedInstruction instruction{};
        std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> ops{};
        if (!decode_record(decoder, record, instruction, ops))
            return fail(OwnedCalleeError::decode_refused);
        const auto next_rip = record.rip + instruction.length;
        const auto visible = instruction.operand_count_visible;

        if (instruction.mnemonic == ZYDIS_MNEMONIC_CALL) {
            if (!import_stop(image, instruction, ops[0], record.rip, state))
                return fail(OwnedCalleeError::missing_import_metadata);
            // CALL is not retired; there is no hypothetical Sony service
            // response and the CPU still points at the unresolved CALL.
            return OwnedCalleeResult::success(std::move(state));
        }

        if (instruction.mnemonic == ZYDIS_MNEMONIC_PUSH &&
            visible == 1U &&
            ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            ops[0].size == 64U) {
            const auto* src = register_slot(
                state.context, ops[0].reg.value);
            if (src == nullptr || state.context.rsp < 8U)
                return fail(OwnedCalleeError::unsupported_operand);
            const auto value = *src;
            const auto next_sp = state.context.rsp - 8U;
            if (!guest_stack_write(state, next_sp, value))
                return fail(OwnedCalleeError::stack_out_of_bounds);
            state.context.rsp = next_sp;
        } else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV &&
                   visible == 2U &&
                   ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                   ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                   ops[0].size == 64U && ops[1].size == 64U) {
            auto* dst = register_slot(state.context, ops[0].reg.value);
            const auto* src = register_slot(
                state.context, ops[1].reg.value);
            if (dst == nullptr || src == nullptr)
                return fail(OwnedCalleeError::unsupported_operand);
            *dst = *src;
        } else if (instruction.mnemonic == ZYDIS_MNEMONIC_SUB &&
                   visible == 2U &&
                   ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                   ops[0].size == 64U &&
                   ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
            auto* dst = register_slot(state.context, ops[0].reg.value);
            if (dst == nullptr || ops[1].imm.value.s < 0)
                return fail(OwnedCalleeError::unsupported_operand);
            const auto lhs = *dst;
            const auto rhs = static_cast<std::uint64_t>(
                ops[1].imm.value.s);
            const auto result = lhs - rhs;
            if (ops[0].reg.value == ZYDIS_REGISTER_RSP &&
                (result < state.guest_stack_base ||
                 result >= state.guest_stack_base +
                             state.guest_stack_bytes.size()))
                return fail(OwnedCalleeError::stack_out_of_bounds);
            *dst = result;
            state.context.rflags = (state.context.rflags &
                ~kArithmeticFlags) | subtract_flags(lhs, rhs, result);
        } else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV &&
                   visible == 2U &&
                   ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                   ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
                   ops[0].size == 64U) {
            if (!write_stack_operand(state, ops[0], next_rip,
                                     static_cast<std::uint64_t>(
                                         ops[1].imm.value.s)))
                return fail(OwnedCalleeError::stack_out_of_bounds);
        } else {
            return fail(OwnedCalleeError::unsupported_instruction);
        }
        state.context.rip = next_rip;
        ++state.retired_instructions;
    }
    return fail(OwnedCalleeError::unsupported_instruction);
}
} // namespace astraea::research
