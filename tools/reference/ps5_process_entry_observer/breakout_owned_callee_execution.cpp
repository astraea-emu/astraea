#include "breakout_owned_callee_execution.hpp"
#include "owned_cpu_step.hpp"
#include "owned_guest_address_space.hpp"
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

// Only a non-retired indirect CALL needs its explicit operand decoded
// here; general instruction semantics belong to step_owned_x86_64.
[[nodiscard]] bool decode_call_operand(
    const DecodedSourceInstruction& record,
    ZydisDecodedInstruction& insn,
    std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT>& operands) {
    ZydisDecoder decoder{};
    if (ZYAN_FAILED(ZydisDecoderInit(
            &decoder, ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64)) ||
        ZYAN_FAILED(ZydisDecoderDecodeFull(
            &decoder, record.bytes.data(), record.length,
            &insn, operands.data())))
        return false;
    return insn.length == record.length &&
        insn.mnemonic == static_cast<ZydisMnemonic>(record.mnemonic);
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
                       instruction.length)
        return false;
    const auto address = owned_cpu_memory_address(
        state.context, mem, call_rip + instruction.length);
    if (!address.has_value()) return false;
    slot = address.value();
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
    const auto fail = [](OwnedCalleeError code) {
        return OwnedCalleeResult::failure(code);
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

    const auto perms = m::GuestPermissions::checked_from_bits(
        static_cast<std::uint8_t>(m::GuestPermission::read) |
        static_cast<std::uint8_t>(m::GuestPermission::write));
    if (!perms.has_value())
        return fail(OwnedCalleeError::stack_out_of_bounds);
    OwnedGuestAddressSpace memory;
    if (!memory.map(state.guest_stack_base, state.guest_stack_bytes,
                    perms.value()).has_value())
        return fail(OwnedCalleeError::stack_out_of_bounds);

    // The entry CALL was already retired in #432. Reconstruct its exact
    // owned simulated return word but do not count it among callee writes.
    if (!memory.write(
            entry.guest_stack_write_address,
            entry.guest_stack_return_word).has_value())
        return fail(OwnedCalleeError::stack_out_of_bounds);

    for (const auto& record : decoded->instructions) {
        const auto result = step_owned_x86_64(
            record, state.context, memory);
        if (result.has_value()) {
            ++state.retired_instructions;
            state.guest_stack_writes += result->guest_writes;
            continue;
        }
        const auto& stop = result.error();
        if (stop.code == OwnedCpuStepStopCode::unresolved_control_transfer) {
            // A CPU step cannot infer a Sony provider. Breakout-specific
            // import metadata is checked *after* a no-retirement stop.
            ZydisDecodedInstruction insn{};
            std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> ops{};
            if (!decode_call_operand(record, insn, ops) ||
                insn.mnemonic != ZYDIS_MNEMONIC_CALL)
                return fail(OwnedCalleeError::unsupported_instruction);
            if (!import_stop(image, insn, ops[0], record.rip, state))
                return fail(OwnedCalleeError::missing_import_metadata);
            return OwnedCalleeResult::success(std::move(state));
        }
        if (stop.code == OwnedCpuStepStopCode::decode_refused)
            return fail(OwnedCalleeError::decode_refused);
        if (stop.code == OwnedCpuStepStopCode::guest_memory_refused)
            return fail(OwnedCalleeError::stack_out_of_bounds);
        if (stop.code == OwnedCpuStepStopCode::unsupported_operand ||
            stop.code == OwnedCpuStepStopCode::address_overflow)
            return fail(OwnedCalleeError::unsupported_operand);
        return fail(OwnedCalleeError::unsupported_instruction);
    }
    return fail(OwnedCalleeError::unsupported_instruction);
}
} // namespace astraea::research
