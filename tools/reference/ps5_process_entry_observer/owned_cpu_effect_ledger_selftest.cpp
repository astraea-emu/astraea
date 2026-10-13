// Independent research-only ledger tests. No external ELF, Sony services,
// native guest entry, or assertion of general x86-64 CPU conformance.
#include "owned_cpu_effect_ledger.hpp"
#include "owned_cpu_step.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {
namespace r = astraea::research;
namespace e = astraea::execution;
namespace m = astraea::memory;

[[nodiscard]] bool map(
    r::OwnedGuestAddressSpace& space, std::uint64_t base,
    std::span<std::byte> bytes, std::uint8_t bits) {
    const auto permissions = m::GuestPermissions::checked_from_bits(bits);
    return permissions.has_value() &&
           space.map(base, bytes, permissions.value()).has_value();
}

[[nodiscard]] bool checked_owned_step_bridge() {
    // Real current Astraea-owned PUSH decoder/interpreter result, not a
    // fabricated test-only CPU state. Its committed store is checked again
    // through the guest-owned readback before the report is produced.
    std::array<std::byte, 256U> stack{};
    r::OwnedGuestAddressSpace memory;
    if (!map(memory, 0x4000U, stack, 3U)) return false;
    e::GuestCpuContext cpu{};
    cpu.rip = 0x210U;
    cpu.rsp = 0x4080U;
    cpu.rbp = 0x1122334455667788ULL;
    cpu.rflags = 2U;
    const auto before = cpu;
    r::OwnedCpuEffectLedger ledger;
    if (!ledger.begin(before)) return false;
    r::DecodedSourceInstruction push{};
    push.rip = before.rip;
    push.length = 1U;
    push.mnemonic = static_cast<std::uint32_t>(ZYDIS_MNEMONIC_PUSH);
    push.bytes[0] = std::byte{0x55};
    const auto committed = r::step_owned_x86_64(push, cpu, memory);
    if (!committed.has_value() || committed->guest_write_size != 8U ||
        !committed->guest_write_address.has_value() ||
        committed->guest_writes != 1U)
        return false;
    const auto addr = committed->guest_write_address.value();
    if (!ledger.append_completed_write(
            addr, std::span<const std::byte>(
                committed->guest_write_bytes.data(),
                committed->guest_write_size), memory))
        return false;
    r::OwnedCpuEffectReport report{};
    if (!ledger.finish(r::OwnedEffectOutcome::retired, cpu,
                       r::OwnedEffectFault::none, std::nullopt, report) ||
        report.outcome != r::OwnedEffectOutcome::retired ||
        report.write_count != 1U ||
        report.writes[0].guest_address != 0x4078U ||
        report.writes[0].size != 8U ||
        report.before_context != before || report.after_context != cpu ||
        report.after_context.rip != 0x211U)
        return false;
    if (ledger.begin(cpu) || ledger.finish(
            r::OwnedEffectOutcome::retired, cpu,
            r::OwnedEffectFault::none, std::nullopt, report))
        return false;
    return true;
}

[[nodiscard]] bool checked_preflight_refusal() {
    std::array<std::byte, 32U> ro_bytes{};
    r::OwnedGuestAddressSpace memory;
    if (!map(memory, 0x5000U, ro_bytes, 1U)) return false;
    e::GuestCpuContext cpu{};
    cpu.rip = 0x310U;
    cpu.rsp = 0x5010U;
    cpu.rbp = 7U;
    const auto before = cpu;
    r::DecodedSourceInstruction push{};
    push.rip = cpu.rip;
    push.length = 1U;
    push.mnemonic = static_cast<std::uint32_t>(ZYDIS_MNEMONIC_PUSH);
    push.bytes[0] = std::byte{0x55};
    r::OwnedCpuEffectLedger ledger;
    if (!ledger.begin(before)) return false;
    const auto stopped = r::step_owned_x86_64(push, cpu, memory);
    if (stopped.has_value() || cpu != before ||
        stopped.error().code != r::OwnedCpuStepStopCode::guest_memory_refused ||
        !stopped.error().memory_fault.has_value() ||
        stopped.error().memory_fault->code !=
            r::OwnedGuestMemoryError::permission_denied)
        return false;
    r::OwnedCpuEffectReport report{};
    if (!ledger.finish(r::OwnedEffectOutcome::refused_before_effects, cpu,
                       r::OwnedEffectFault::memory_permission,
                       0x5008U, report) ||
        report.write_count != 0U ||
        report.after_context != before ||
        report.fault_address != 0x5008U ||
        ro_bytes != std::array<std::byte, 32U>{})
        return false;
    return true;
}

[[nodiscard]] bool multiple_and_wide_effects() {
    std::array<std::byte, 256U> guest{};
    r::OwnedGuestAddressSpace memory;
    if (!map(memory, 0x7000U, guest, 3U)) return false;
    std::array<std::byte, 16U> wide{};
    for (std::size_t i = 0; i < wide.size(); ++i)
        wide[i] = static_cast<std::byte>(i + 1U);
    constexpr std::array<std::byte, 2U> small{
        std::byte{0xa5}, std::byte{0xb6}};
    e::GuestCpuContext before{};
    before.rip = 0x410U;
    r::OwnedCpuEffectLedger ledger;
    if (!ledger.begin(before) ||
        !memory.write(0x7010U, wide).has_value() ||
        !ledger.append_completed_write(0x7010U, wide, memory) ||
        !memory.write(0x7030U, small).has_value() ||
        !ledger.append_completed_write(0x7030U, small, memory))
        return false;
    auto after = before;
    after.rip = 0x414U;
    r::OwnedCpuEffectReport report{};
    return ledger.finish(r::OwnedEffectOutcome::retired, after,
                         r::OwnedEffectFault::none, std::nullopt, report) &&
           report.write_count == 2U &&
           report.writes[0].size == 16U &&
           report.writes[1].size == 2U &&
           report.writes[0].bytes[15] == std::byte{16} &&
           report.writes[1].bytes[1] == std::byte{0xb6};
}

[[nodiscard]] bool partial_fault_and_restart() {
    // Source-owned model of the *observed* Unicorn REP MOVSB fault. The
    // current Astraea interpreter does NOT execute REP; state below is
    // explicit research input, not claimed emulated CPU execution.
    std::array<std::byte, 1U> writable{};
    std::array<std::byte, 4U> read_only{};
    read_only.fill(std::byte{0x77});
    r::OwnedGuestAddressSpace memory;
    if (!map(memory, 0x8fffU, writable, 3U) ||
        !map(memory, 0x9000U, read_only, 1U)) return false;
    e::GuestCpuContext before{};
    before.rip = 0x510U;
    before.rcx = 3U;
    before.rsi = 0xa100U;
    before.rdi = 0x8fffU;
    r::OwnedCpuEffectLedger ledger;
    if (!ledger.begin(before)) return false;
    constexpr std::array<std::byte, 1U> first{std::byte{0xa5}};
    constexpr std::array<std::byte, 1U> denied{std::byte{0xb6}};
    if (!memory.write(before.rdi, first).has_value() ||
        !ledger.append_completed_write(before.rdi, first, memory))
        return false;
    const auto fail = memory.write(0x9000U, denied);
    if (fail.has_value() ||
        fail.error().code != r::OwnedGuestMemoryError::permission_denied)
        return false;
    auto after = before;
    ++after.rsi;
    ++after.rdi;
    --after.rcx;
    r::OwnedCpuEffectReport report{};
    if (!ledger.finish(r::OwnedEffectOutcome::fault_after_effects, after,
                       r::OwnedEffectFault::memory_permission, 0x9000U,
                       report))
        return false;
    return report.outcome == r::OwnedEffectOutcome::fault_after_effects &&
           report.instruction_rip == after.rip &&
           report.write_count == 1U &&
           report.writes[0].guest_address == 0x8fffU &&
           report.writes[0].bytes[0] == first[0] &&
           report.after_context.rcx == 2U &&
           report.after_context.rdi == 0x9000U &&
           writable[0] == first[0] &&
           read_only == std::array<std::byte, 4U>{
               std::byte{0x77}, std::byte{0x77},
               std::byte{0x77}, std::byte{0x77}};
}

[[nodiscard]] bool fail_closed_negatives() {
    std::array<std::byte, 128U> backing{};
    r::OwnedGuestAddressSpace memory;
    if (!map(memory, 0xb000U, backing, 3U)) return false;
    e::GuestCpuContext cpu{};
    cpu.rip = 0x610U;
    r::OwnedCpuEffectReport output{};
    constexpr std::array<std::byte, 1U> mark{std::byte{0x91}};

    // Cannot claim a write solely on the basis of a pre-write callback:
    // its proposed value disagrees with still-unchanged guest readback.
    r::OwnedCpuEffectLedger uncommitted;
    if (!uncommitted.begin(cpu) ||
        uncommitted.append_completed_write(0xb000U, mark, memory) ||
        uncommitted.finish(r::OwnedEffectOutcome::retired, cpu,
                           r::OwnedEffectFault::none, std::nullopt, output))
        return false;

    // Finite ledgers refuse an overflowing REP rather than returning a
    // misleading complete instruction report with a truncated write list.
    r::OwnedCpuEffectLedger overflow;
    if (!overflow.begin(cpu)) return false;
    for (std::size_t i = 0U; i < r::OwnedCpuEffectReport::kMaxEvents; ++i) {
        if (!memory.write(0xb000U + i, mark).has_value() ||
            !overflow.append_completed_write(0xb000U + i, mark, memory))
            return false;
    }
    if (!memory.write(0xb040U, mark).has_value() ||
        overflow.append_completed_write(0xb040U, mark, memory) ||
        overflow.finish(r::OwnedEffectOutcome::retired, cpu,
                        r::OwnedEffectFault::none, std::nullopt, output))
        return false;

    // A guest-writable but unreadable mapping cannot be attested by this
    // readback-based research verifier.
    std::array<std::byte, 8U> unreadable{};
    r::OwnedGuestAddressSpace write_only;
    if (!map(write_only, 0xc000U, unreadable, 2U) ||
        !write_only.write(0xc000U, mark).has_value()) return false;
    r::OwnedCpuEffectLedger no_readback;
    if (!no_readback.begin(cpu) ||
        no_readback.append_completed_write(0xc000U, mark, write_only))
        return false;

    // Once a completed write is recorded, neither a pre-effect refusal nor
    // a nonsensical "partial fault" with advanced RIP may be accepted.
    r::OwnedCpuEffectLedger contradictory;
    if (!contradictory.begin(cpu) ||
        !contradictory.append_completed_write(0xb000U, mark, memory))
        return false;
    if (contradictory.finish(r::OwnedEffectOutcome::refused_before_effects,
                             cpu, r::OwnedEffectFault::memory_permission,
                             0xb001U, output))
        return false;
    r::OwnedCpuEffectLedger moved_rip;
    if (!moved_rip.begin(cpu) ||
        !moved_rip.append_completed_write(0xb000U, mark, memory))
        return false;
    auto advanced = cpu;
    advanced.rip += 2U;
    if (moved_rip.finish(r::OwnedEffectOutcome::fault_after_effects,
                         advanced, r::OwnedEffectFault::memory_permission,
                         0xb001U, output))
        return false;

    // Zero width, excessive width and overflow are all independently
    // rejected, rather than being recorded with ambiguous effect sizes.
    r::OwnedCpuEffectLedger invalid_width;
    if (!invalid_width.begin(cpu) ||
        invalid_width.append_completed_write(
            0xb000U, std::span<const std::byte>{}, memory))
        return false;
    std::array<std::byte, 65U> too_wide{};
    r::OwnedCpuEffectLedger too_large;
    if (!too_large.begin(cpu) ||
        too_large.append_completed_write(0xb000U, too_wide, memory))
        return false;
    r::OwnedCpuEffectLedger bad_addr;
    if (!bad_addr.begin(cpu) ||
        bad_addr.append_completed_write(UINT64_MAX, mark, memory))
        return false;
    return true;
}
} // namespace

int main() {
    if (!checked_owned_step_bridge() || !checked_preflight_refusal() ||
        !multiple_and_wide_effects() || !partial_fault_and_restart() ||
        !fail_closed_negatives()) {
        std::fputs("C1 OWNED EFFECT LEDGER REFUSED\n", stderr);
        return 1;
    }
    std::puts("C1 OWNED EFFECT LEDGER PASS");
    std::puts("owned_interpreter_store=readback_verified");
    std::puts("owned_interpreter_preflight_refusal=no_effects");
    std::puts("variable_width_and_multiple_writes=recorded");
    std::puts("partial_fault=prefix_preserved_restart_state");
    std::puts("hook_value_alone=not_commit_evidence");
    std::puts("overflow_and_unreadable=fail_closed");
    std::puts("backend_adoption=not_authorized");
    std::puts("native_outside_authored_execution=none");
    return 0;
}
