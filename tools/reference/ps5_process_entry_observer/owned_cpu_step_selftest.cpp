// Fully source-owned CPU instruction-step conformance, separate from Breakout.
// No external ELF and no native guest instruction execution.
#include "owned_cpu_step.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace {
namespace r = astraea::research;
namespace e = astraea::execution;
namespace m = astraea::memory;

[[nodiscard]] r::DecodedSourceInstruction record(
    std::uint64_t rip, std::initializer_list<unsigned char> bytes,
    ZydisMnemonic mnemonic) {
    r::DecodedSourceInstruction result{};
    result.rip = rip;
    result.length = static_cast<std::uint8_t>(bytes.size());
    result.mnemonic = static_cast<std::uint32_t>(mnemonic);
    std::size_t i = 0U;
    for (const auto byte : bytes)
        result.bytes[i++] = static_cast<std::byte>(byte);
    return result;
}

[[nodiscard]] bool test_owned_steps() {
    const auto rw = m::GuestPermissions::checked_from_bits(3U);
    const auto ro = m::GuestPermissions::checked_from_bits(1U);
    if (!rw.has_value() || !ro.has_value()) return false;

    std::array<std::byte, 4096U> stack{};
    r::OwnedGuestAddressSpace memory;
    if (!memory.map(0x1000U, stack, rw.value()).has_value())
        return false;

    e::GuestCpuContext cpu{};
    cpu.rip = 0x210U;
    cpu.rsp = 0x1ff0U;
    cpu.rbp = 0x1122334455667788ULL;
    cpu.rflags = 0x202U;
    const auto push = record(0x210U, {0x55}, ZYDIS_MNEMONIC_PUSH);
    const auto mov = record(0x211U, {0x48, 0x89, 0xe5}, ZYDIS_MNEMONIC_MOV);
    const auto sub = record(0x214U,
        {0x48, 0x81, 0xec, 0xe8, 0, 0, 0}, ZYDIS_MNEMONIC_SUB);
    const auto store = record(0x21bU,
        {0x48, 0xc7, 0x45, 0x90, 0, 0, 0, 0}, ZYDIS_MNEMONIC_MOV);
    const auto call = record(0x223U,
        {0xff, 0x15, 0x86, 0x66, 0, 0}, ZYDIS_MNEMONIC_CALL);

    const auto a = r::step_owned_x86_64(push, cpu, memory);
    if (!a.has_value() || a->guest_writes != 1U ||
        cpu.rip != 0x211U || cpu.rsp != 0x1fe8U ||
        cpu.rbp != 0x1122334455667788ULL)
        return false;
    std::array<std::byte, 8U> saved{};
    if (!memory.read(0x1fe8U, saved).has_value() ||
        saved[0] != std::byte{0x88} ||
        saved[7] != std::byte{0x11})
        return false;

    const auto b = r::step_owned_x86_64(mov, cpu, memory);
    if (!b.has_value() || b->guest_writes != 0U ||
        cpu.rbp != 0x1fe8U || cpu.rip != 0x214U)
        return false;
    const auto c = r::step_owned_x86_64(sub, cpu, memory);
    if (!c.has_value() || c->guest_writes != 0U ||
        cpu.rsp != 0x1f00U || cpu.rip != 0x21bU ||
        (cpu.rflags & 0x2U) == 0U)
        return false;
    const auto d = r::step_owned_x86_64(store, cpu, memory);
    if (!d.has_value() || d->guest_writes != 1U ||
        cpu.rip != 0x223U)
        return false;
    std::array<std::byte, 8U> written{};
    written.fill(std::byte{0xaa});
    if (!memory.read(0x1f78U, written).has_value())
        return false;
    for (const auto byte : written)
        if (byte != std::byte{0}) return false;

    const auto before_call = cpu;
    const auto before_stack = stack;
    const auto stopped = r::step_owned_x86_64(call, cpu, memory);
    if (stopped.has_value() ||
        stopped.error().code !=
            r::OwnedCpuStepStopCode::unresolved_control_transfer ||
        stopped.error().guest_rip != 0x223U ||
        cpu != before_call || stack != before_stack)
        return false;

    // Invalid decoder/record length and wrong RIP refuse before retirement.
    const auto wrong_rip = r::step_owned_x86_64(push, cpu, memory);
    if (wrong_rip.has_value() ||
        wrong_rip.error().code != r::OwnedCpuStepStopCode::context_mismatch ||
        cpu != before_call)
        return false;
    auto malformed = call;
    malformed.mnemonic = static_cast<std::uint32_t>(ZYDIS_MNEMONIC_PUSH);
    const auto bad_record = r::step_owned_x86_64(malformed, cpu, memory);
    if (bad_record.has_value() ||
        bad_record.error().code != r::OwnedCpuStepStopCode::decode_refused ||
        cpu != before_call)
        return false;
    const auto ud2 = record(0x223U, {0x0f, 0x0b}, ZYDIS_MNEMONIC_UD2);
    const auto bad_opcode = r::step_owned_x86_64(ud2, cpu, memory);
    if (bad_opcode.has_value() ||
        bad_opcode.error().code != r::OwnedCpuStepStopCode::unsupported_instruction ||
        cpu != before_call)
        return false;

    // A mapped-but-read-only target refuses a PUSH, retaining CPU and data.
    std::array<std::byte, 64U> protected_bytes{};
    r::OwnedGuestAddressSpace protected_memory;
    if (!protected_memory.map(0x5000U, protected_bytes,
                              ro.value()).has_value())
        return false;
    e::GuestCpuContext denied{};
    denied.rip = 0x210U;
    denied.rsp = 0x5020U;
    denied.rbp = 0xaabbccddU;
    const auto before_denied = denied;
    const auto no_write = r::step_owned_x86_64(
        push, denied, protected_memory);
    if (no_write.has_value() ||
        no_write.error().code !=
            r::OwnedCpuStepStopCode::guest_memory_refused ||
        !no_write.error().memory_fault.has_value() ||
        no_write.error().memory_fault->code !=
            r::OwnedGuestMemoryError::permission_denied ||
        denied != before_denied ||
        protected_bytes != std::array<std::byte, 64U>{})
        return false;

    // Unmapped target also refuses without altering the instruction pointer.
    denied.rsp = 0x6000U;
    const auto before_unmapped = denied;
    const auto unmapped = r::step_owned_x86_64(
        push, denied, protected_memory);
    if (unmapped.has_value() ||
        unmapped.error().code !=
            r::OwnedCpuStepStopCode::guest_memory_refused ||
        !unmapped.error().memory_fault.has_value() ||
        unmapped.error().memory_fault->code !=
            r::OwnedGuestMemoryError::unmapped ||
        denied != before_unmapped)
        return false;

    // Register SUB flags: 1 - 1 => ZF+PF, CF/OF clear.
    e::GuestCpuContext arithmetic{};
    arithmetic.rip = 0x900U;
    arithmetic.rax = 1U;
    arithmetic.rflags = 0x202U;
    const auto sub_rax = record(
        0x900U, {0x48, 0x83, 0xe8, 0x01}, ZYDIS_MNEMONIC_SUB);
    const auto flags = r::step_owned_x86_64(
        sub_rax, arithmetic, memory);
    if (!flags.has_value() || arithmetic.rax != 0U ||
        arithmetic.rip != 0x904U ||
        (arithmetic.rflags & (1ULL << 6U)) == 0U ||
        (arithmetic.rflags & (1ULL << 2U)) == 0U ||
        (arithmetic.rflags & 1U) != 0U ||
        (arithmetic.rflags & (1ULL << 11U)) != 0U)
        return false;

    return true;
}
} // namespace

int main() {
    if (!test_owned_steps()) {
        std::fputs("C1 OWNED CPU STEP REFUSED\n", stderr);
        return 1;
    }
    std::puts("C1 OWNED CPU STEP PASS");
    std::puts("owned_push_mov_sub_store=retired");
    std::puts("owned_step_guest_writes=checked");
    std::puts("control_transfer_stop=before_retirement");
    std::puts("failed_guest_write=cpu_and_bytes_unchanged");
    std::puts("guest_register_flags=sub_zero_case_verified");
    std::puts("malformed_record_and_unknown_opcode=refused");
    std::puts("breakout_elf_execution=not_part_of_this_test");
    std::puts("native_hostile_code_containment=not_established");
}
