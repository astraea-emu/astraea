#include "owned_cpu_step.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace astraea::research {
namespace {
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

[[nodiscard]] const std::uint64_t* register_slot(
    const e::GuestCpuContext& cpu, ZydisRegister reg) noexcept {
    return register_slot(const_cast<e::GuestCpuContext&>(cpu), reg);
}

[[nodiscard]] bool add_signed(
    std::uint64_t base, std::int64_t delta,
    std::uint64_t& out) noexcept {
    if (delta >= 0) {
        const auto shift = static_cast<std::uint64_t>(delta);
        if (base > std::numeric_limits<std::uint64_t>::max() - shift)
            return false;
        out = base + shift;
    } else {
        const auto magnitude =
            static_cast<std::uint64_t>(-(delta + 1)) + 1U;
        if (base < magnitude) return false;
        out = base - magnitude;
    }
    return true;
}

[[nodiscard]] std::uint64_t subtraction_flags(
    std::uint64_t lhs, std::uint64_t rhs,
    std::uint64_t result) noexcept {
    const bool parity = (std::popcount(
        static_cast<unsigned>(result & 0xffU)) % 2) == 0;
    return (lhs < rhs ? kCF : 0U) |
        (parity ? kPF : 0U) |
        (((lhs ^ rhs ^ result) & 0x10U) != 0U ? kAF : 0U) |
        (result == 0U ? kZF : 0U) |
        ((result & (1ULL << 63U)) != 0U ? kSF : 0U) |
        (((lhs ^ rhs) & (lhs ^ result) & (1ULL << 63U)) != 0U ?
             kOF : 0U);
}

[[nodiscard]] std::array<std::byte, 8U> word_bytes(
    std::uint64_t value) noexcept {
    std::array<std::byte, 8U> bytes{};
    for (std::size_t i = 0U; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::byte>(
            (value >> (i * 8U)) & 0xffU);
    return bytes;
}

[[nodiscard]] OwnedCpuStepResult refuse(
    OwnedCpuStepStopCode code, std::uint64_t rip) {
    return OwnedCpuStepResult::failure(OwnedCpuStepStop{
        .code = code,
        .guest_rip = rip,
        .memory_fault = std::nullopt,
    });
}

[[nodiscard]] OwnedCpuStepResult memory_refusal(
    std::uint64_t rip, OwnedGuestMemoryFault fault) {
    return OwnedCpuStepResult::failure(OwnedCpuStepStop{
        .code = OwnedCpuStepStopCode::guest_memory_refused,
        .guest_rip = rip,
        .memory_fault = fault,
    });
}
} // namespace

std::optional<std::uint64_t> owned_cpu_memory_address(
    const e::GuestCpuContext& cpu,
    const ZydisDecodedOperand& operand,
    std::uint64_t next_rip) noexcept {
    if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY ||
        operand.mem.index != ZYDIS_REGISTER_NONE ||
        (operand.mem.scale != 0U && operand.mem.scale != 1U) ||
        (operand.mem.segment != ZYDIS_REGISTER_NONE &&
         operand.mem.segment != ZYDIS_REGISTER_DS &&
         operand.mem.segment != ZYDIS_REGISTER_SS))
        return std::nullopt;
    std::uint64_t base = 0U;
    if (operand.mem.base == ZYDIS_REGISTER_RIP) {
        base = next_rip;
    } else {
        const auto* reg = register_slot(cpu, operand.mem.base);
        if (reg == nullptr) return std::nullopt;
        base = *reg;
    }
    std::uint64_t address = 0U;
    if (!add_signed(base, operand.mem.disp.has_displacement ?
                          operand.mem.disp.value : 0, address))
        return std::nullopt;
    return address;
}

OwnedCpuStepResult step_owned_x86_64(
    const DecodedSourceInstruction& record,
    e::GuestCpuContext& cpu,
    OwnedGuestAddressSpace& memory) {
    using Stop = OwnedCpuStepStopCode;
    if (record.rip != cpu.rip)
        return refuse(Stop::context_mismatch, cpu.rip);
    ZydisDecoder decoder{};
    if (record.length == 0U || record.length > record.bytes.size() ||
        ZYAN_FAILED(ZydisDecoderInit(
            &decoder, ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64)))
        return refuse(Stop::decode_refused, cpu.rip);
    ZydisDecodedInstruction insn{};
    std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> ops{};
    if (ZYAN_FAILED(ZydisDecoderDecodeFull(
            &decoder, record.bytes.data(), record.length,
            &insn, ops.data())) ||
        insn.length != record.length ||
        insn.mnemonic != static_cast<ZydisMnemonic>(record.mnemonic))
        return refuse(Stop::decode_refused, cpu.rip);
    if (record.rip > std::numeric_limits<std::uint64_t>::max() -
                         insn.length)
        return refuse(Stop::address_overflow, cpu.rip);
    const auto next = record.rip + insn.length;
    const auto visible = insn.operand_count_visible;

    // No generic indirect/control-flow semantics are admitted here.
    // Import metadata and typed NID identity are a caller-level boundary.
    if (insn.mnemonic == ZYDIS_MNEMONIC_CALL ||
        insn.mnemonic == ZYDIS_MNEMONIC_JMP ||
        insn.mnemonic == ZYDIS_MNEMONIC_RET ||
        insn.mnemonic == ZYDIS_MNEMONIC_SYSCALL)
        return refuse(Stop::unresolved_control_transfer, cpu.rip);

    auto pending = cpu;
    std::optional<std::uint64_t> guest_write_address;
    std::uint64_t guest_write_value = 0U;

    if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH &&
        visible == 1U && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        ops[0].size == 64U) {
        const auto* src = register_slot(cpu, ops[0].reg.value);
        if (src == nullptr || cpu.rsp < 8U)
            return refuse(Stop::unsupported_operand, cpu.rip);
        guest_write_address = cpu.rsp - 8U;
        guest_write_value = *src; // x86 PUSH RSP uses the pre-decrement value
        pending.rsp = guest_write_address.value();
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV &&
               visible == 2U &&
               ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               ops[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               ops[0].size == 64U && ops[1].size == 64U) {
        auto* dst = register_slot(pending, ops[0].reg.value);
        const auto* src = register_slot(cpu, ops[1].reg.value);
        if (dst == nullptr || src == nullptr)
            return refuse(Stop::unsupported_operand, cpu.rip);
        *dst = *src;
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_SUB &&
               visible == 2U &&
               ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               ops[0].size == 64U &&
               ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
        auto* dst = register_slot(pending, ops[0].reg.value);
        const auto* src = register_slot(cpu, ops[0].reg.value);
        if (dst == nullptr || src == nullptr)
            return refuse(Stop::unsupported_operand, cpu.rip);
        const auto lhs = *src;
        const auto rhs = static_cast<std::uint64_t>(
            ops[1].imm.value.s);
        const auto result = lhs - rhs;
        if (ops[0].reg.value == ZYDIS_REGISTER_RSP &&
            rhs > lhs)
            return refuse(Stop::unsupported_operand, cpu.rip);
        *dst = result;
        pending.rflags = (cpu.rflags & ~kArithmeticFlags) |
            subtraction_flags(lhs, rhs, result);
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV &&
               visible == 2U &&
               ops[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
               ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
               ops[0].size == 64U) {
        guest_write_address = owned_cpu_memory_address(
            cpu, ops[0], next);
        if (!guest_write_address.has_value())
            return refuse(Stop::unsupported_operand, cpu.rip);
        guest_write_value = static_cast<std::uint64_t>(
            ops[1].imm.value.s);
    } else {
        return refuse(Stop::unsupported_instruction, cpu.rip);
    }

    std::size_t guest_writes = 0U;
    if (guest_write_address.has_value()) {
        const auto word = word_bytes(guest_write_value);
        const auto write = memory.write(
            guest_write_address.value(), word);
        if (!write.has_value())
            return memory_refusal(cpu.rip, write.error());
        guest_writes = 1U;
    }
    pending.rip = next;
    cpu = pending;
    return OwnedCpuStepResult::success(OwnedCpuStepCommit{
        .next_rip = next,
        .guest_writes = guest_writes,
    });
}

OwnedCpuRunReport run_owned_x86_64(
    e::GuestCpuContext& cpu, OwnedGuestAddressSpace& memory,
    std::size_t instruction_budget) {
    OwnedCpuRunReport report{};
    report.stop_rip = cpu.rip;
    if (instruction_budget == 0U ||
        instruction_budget > kC1MaxOwnedRunInstructions)
        return report;

    ZydisDecoder decoder{};
    if (ZYAN_FAILED(ZydisDecoderInit(
            &decoder, ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64))) {
        report.stop = OwnedCpuRunStopCode::invalid_encoding;
        return report;
    }

    for (std::size_t step_index = 0U;
         step_index < instruction_budget; ++step_index) {
        report.stop_rip = cpu.rip;
        std::array<std::byte, kX86MaximumInstructionBytes> bytes{};
        std::size_t available = 0U;
        std::optional<OwnedGuestMemoryFault> fetch_fault;

        // Fetch from the *current* guest virtual image. Never trust an
        // earlier decoded record if code has been changed or remapped.
        // Fetching one byte at a time permits an instruction to end just
        // before an inaccessible page (and permits adjacent mapped spans).
        // A speculative fetch failure after a valid instruction's last
        // byte does not itself fault the shorter valid instruction.
        for (std::size_t n = 0U; n < bytes.size(); ++n) {
            if (cpu.rip > std::numeric_limits<std::uint64_t>::max() - n) {
                fetch_fault = OwnedGuestMemoryFault{
                    OwnedGuestMemoryError::address_overflow, cpu.rip};
                break;
            }
            const auto addr = cpu.rip + n;
            auto fetched = memory.fetch(
                addr, std::span<std::byte>{bytes.data() + n, 1U});
            if (!fetched.has_value()) {
                fetch_fault = fetched.error();
                break;
            }
            ++available;
        }

        if (available == 0U) {
            report.stop = OwnedCpuRunStopCode::fetch_refused;
            report.memory_fault = fetch_fault;
            return report;
        }

        ZydisDecodedInstruction instruction{};
        std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands{};
        if (ZYAN_FAILED(ZydisDecoderDecodeFull(
                &decoder, bytes.data(), available,
                &instruction, operands.data())) ||
            instruction.length == 0U ||
            instruction.length > available ||
            instruction.length > bytes.size()) {
            report.stop = fetch_fault.has_value() ?
                OwnedCpuRunStopCode::fetch_refused :
                OwnedCpuRunStopCode::invalid_encoding;
            report.memory_fault = fetch_fault;
            return report;
        }

        DecodedSourceInstruction record{};
        record.rip = cpu.rip;
        record.length = instruction.length;
        record.mnemonic = static_cast<std::uint32_t>(
            instruction.mnemonic);
        for (std::size_t n = 0U; n < instruction.length; ++n)
            record.bytes[n] = bytes[n];

        auto committed = step_owned_x86_64(record, cpu, memory);
        if (!committed.has_value()) {
            report.stop = OwnedCpuRunStopCode::step_refused;
            report.step_stop = committed.error();
            return report;
        }
        ++report.retired_instructions;
        report.guest_writes += committed->guest_writes;
    }

    report.stop = OwnedCpuRunStopCode::budget_exhausted;
    report.stop_rip = cpu.rip;
    return report;
}

} // namespace astraea::research
