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

[[nodiscard]] bool test_owned_fetch_step() {
    const auto x = m::GuestPermissions::checked_from_bits(4U);
    const auto rw = m::GuestPermissions::checked_from_bits(3U);
    const auto ro = m::GuestPermissions::checked_from_bits(1U);
    const auto rwx = m::GuestPermissions::checked_from_bits(7U);
    if (!x.has_value() || !rw.has_value() || !ro.has_value() ||
        !rwx.has_value()) return false;

    // Independent source-owned program. No Breakout bytes, imports,
    // source-supplied decoded records, or host-native guest execution.
    // PUSH RBP; MOV RBP,RSP; SUB RSP,16;
    // MOV qword [RBP-8],0; CALL [RIP+0x6686] (must NOT retire).
    std::array<std::byte, 22U> code{
        std::byte{0x55},
        std::byte{0x48}, std::byte{0x89}, std::byte{0xe5},
        std::byte{0x48}, std::byte{0x83}, std::byte{0xec},
        std::byte{0x10},
        std::byte{0x48}, std::byte{0xc7}, std::byte{0x45},
        std::byte{0xf8}, std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0},
        std::byte{0xff}, std::byte{0x15}, std::byte{0x86},
        std::byte{0x66}, std::byte{0}, std::byte{0},
    };
    std::array<std::byte, 256U> stack{};
    r::OwnedGuestAddressSpace memory;
    if (!memory.map(0x200U, code, x.value()).has_value() ||
        !memory.map(0x1000U, stack, rw.value()).has_value())
        return false;
    e::GuestCpuContext cpu{};
    cpu.rip = 0x200U;
    cpu.rsp = 0x1080U;
    cpu.rbp = 0x1122334455667788ULL;
    const auto invalid_budget = r::run_owned_x86_64(cpu, memory, 0U);
    if (invalid_budget.stop != r::OwnedCpuRunStopCode::invalid_budget ||
        cpu.rip != 0x200U) return false;
    const auto excessive = r::run_owned_x86_64(
        cpu, memory, r::kC1MaxOwnedRunInstructions + 1U);
    if (excessive.stop != r::OwnedCpuRunStopCode::invalid_budget ||
        excessive.retired_instructions != 0U ||
        cpu.rip != 0x200U) return false;

    // A too-small caller trace cannot yield a truncated *successful*
    // reference. Refuse BEFORE any instruction/state/memory mutation.
    std::array<r::OwnedCpuTraceStep, 4U> reference_trace{};
    const auto state_before_trace = cpu;
    const auto stack_before_trace = stack;
    const auto short_trace = r::run_owned_x86_64(
        cpu, memory, 4U,
        std::span<r::OwnedCpuTraceStep>{reference_trace}.first(2U));
    if (short_trace.stop != r::OwnedCpuRunStopCode::invalid_budget ||
        short_trace.retired_instructions != 0U ||
        short_trace.trace_steps != 0U ||
        cpu != state_before_trace || stack != stack_before_trace ||
        reference_trace !=
            std::array<r::OwnedCpuTraceStep, 4U>{})
        return false;

    const auto prefix = r::run_owned_x86_64(
        cpu, memory, 4U, reference_trace);
    if (prefix.stop != r::OwnedCpuRunStopCode::budget_exhausted ||
        prefix.retired_instructions != 4U ||
        prefix.guest_writes != 2U || cpu.rip != 0x210U ||
        cpu.rbp != 0x1078U || cpu.rsp != 0x1068U ||
        stack[0x78U] != std::byte{0x88} ||
        stack[0x7fU] != std::byte{0x11} ||
        stack[0x70U] != std::byte{0}) return false;
    // Compare the full architectural state after every retirement, not
    // merely the final RIP/count. Explicit write records expose the
    // guest-VA and exact bytes that a second backend must reproduce.
    constexpr std::array<std::uint64_t, 4U> expected_rips{
        0x200U, 0x201U, 0x204U, 0x208U,
    };
    constexpr std::array<std::uint64_t, 4U> expected_next{
        0x201U, 0x204U, 0x208U, 0x210U,
    };
    if (prefix.trace_steps != 4U ||
        reference_trace[3U].after_context != cpu ||
        !reference_trace[0U].guest_write_address.has_value() ||
        reference_trace[0U].guest_write_address.value() != 0x1078U ||
        reference_trace[0U].guest_write_bytes[0] != std::byte{0x88} ||
        reference_trace[0U].guest_write_bytes[7] != std::byte{0x11} ||
        reference_trace[1U].guest_write_address.has_value() ||
        reference_trace[2U].guest_write_address.has_value() ||
        reference_trace[3U].guest_write_address != 0x1070U ||
        reference_trace[3U].guest_write_bytes !=
            std::array<std::byte, 8U>{})
        return false;
    for (std::size_t i = 0U; i < reference_trace.size(); ++i) {
        if (reference_trace[i].before_rip != expected_rips[i] ||
            reference_trace[i].after_rip != expected_next[i] ||
            reference_trace[i].after_context.rip != expected_next[i])
            return false;
    }

    const auto before_call = cpu;
    const auto before_stack = stack;
    std::array<r::OwnedCpuTraceStep, 5U> stopped_trace{};
    const auto blocked_call = r::run_owned_x86_64(
        cpu, memory, 5U, stopped_trace);
    if (blocked_call.stop != r::OwnedCpuRunStopCode::step_refused ||
        blocked_call.trace_steps != 0U ||
        stopped_trace != std::array<r::OwnedCpuTraceStep, 5U>{} ||
        blocked_call.stop_rip != 0x210U ||
        blocked_call.retired_instructions != 0U ||
        !blocked_call.step_stop.has_value() ||
        blocked_call.step_stop->code !=
            r::OwnedCpuStepStopCode::unresolved_control_transfer ||
        cpu != before_call || stack != before_stack) return false;

    // Instruction bytes require execute permission, not read permission.
    std::array<std::byte, 1U> no_execute{std::byte{0x55}};
    r::OwnedGuestAddressSpace forbidden;
    if (!forbidden.map(0x300U, no_execute, ro.value()).has_value())
        return false;
    e::GuestCpuContext at_forbidden{};
    at_forbidden.rip = 0x300U;
    const auto refused = r::run_owned_x86_64(
        at_forbidden, forbidden, 1U);
    if (refused.stop != r::OwnedCpuRunStopCode::fetch_refused ||
        !refused.memory_fault.has_value() ||
        refused.memory_fault->code !=
            r::OwnedGuestMemoryError::permission_denied ||
        at_forbidden.rip != 0x300U) return false;

    e::GuestCpuContext at_unmapped{};
    at_unmapped.rip = 0x500U;
    const auto missing = r::run_owned_x86_64(
        at_unmapped, forbidden, 1U);
    if (missing.stop != r::OwnedCpuRunStopCode::fetch_refused ||
        !missing.memory_fault.has_value() ||
        missing.memory_fault->code != r::OwnedGuestMemoryError::unmapped)
        return false;

    // An incomplete two-byte encoding has no permission to decode past
    // the one executable byte. The stopped CPU remains untouched.
    std::array<std::byte, 1U> truncated{std::byte{0x0f}};
    r::OwnedGuestAddressSpace partial;
    if (!partial.map(0x600U, truncated, x.value()).has_value())
        return false;
    e::GuestCpuContext at_partial{};
    at_partial.rip = 0x600U;
    const auto partial_stop = r::run_owned_x86_64(
        at_partial, partial, 1U);
    if (partial_stop.stop != r::OwnedCpuRunStopCode::fetch_refused ||
        partial_stop.retired_instructions != 0U ||
        !partial_stop.memory_fault.has_value() ||
        partial_stop.memory_fault->guest_address != 0x601U ||
        at_partial.rip != 0x600U) return false;

    // A valid short instruction may end at a region boundary. A
    // multi-byte instruction may also span two adjacent executable maps.
    std::array<std::byte, 1U> first{std::byte{0x48}};
    std::array<std::byte, 2U> second{
        std::byte{0x89}, std::byte{0xe5}};
    r::OwnedGuestAddressSpace adjacent;
    if (!adjacent.map(0x900U, first, x.value()).has_value() ||
        !adjacent.map(0x901U, second, x.value()).has_value())
        return false;
    e::GuestCpuContext cross{};
    cross.rip = 0x900U;
    cross.rsp = 0x1234U;
    const auto crossed = r::run_owned_x86_64(cross, adjacent, 1U);
    if (crossed.stop != r::OwnedCpuRunStopCode::budget_exhausted ||
        crossed.retired_instructions != 1U ||
        cross.rip != 0x903U || cross.rbp != 0x1234U) return false;

    // Fresh bytes are fetched on each invocation. Replacing PUSH with
    // UD2 must never execute a cached earlier PUSH record.
    std::array<std::byte, 2U> mutable_code{
        std::byte{0x55}, std::byte{0x90}};
    std::array<std::byte, 64U> mutable_stack{};
    r::OwnedGuestAddressSpace mutable_memory;
    if (!mutable_memory.map(
            0xa00U, mutable_code, rwx.value()).has_value() ||
        !mutable_memory.map(
            0xb00U, mutable_stack, rw.value()).has_value())
        return false;
    e::GuestCpuContext changing{};
    changing.rip = 0xa00U;
    changing.rsp = 0xb30U;
    const auto first_run = r::run_owned_x86_64(
        changing, mutable_memory, 1U);
    if (first_run.retired_instructions != 1U ||
        first_run.stop != r::OwnedCpuRunStopCode::budget_exhausted)
        return false;
    const std::array<std::byte, 2U> ud2{
        std::byte{0x0f}, std::byte{0x0b}};
    if (!mutable_memory.write(0xa00U, ud2).has_value())
        return false;
    changing.rip = 0xa00U;
    const auto unchanged = changing;
    const auto bytes_before_stop = mutable_stack;
    const auto mutated = r::run_owned_x86_64(
        changing, mutable_memory, 1U);
    if (mutated.stop != r::OwnedCpuRunStopCode::step_refused ||
        !mutated.step_stop.has_value() ||
        mutated.step_stop->code !=
            r::OwnedCpuStepStopCode::unsupported_instruction ||
        changing != unchanged || mutable_stack != bytes_before_stop)
        return false;

    // One instruction retires, then a forbidden second-instruction
    // stack write refuses. Prior progress stays, failed state does not.
    std::array<std::byte, 4U> before_denial{
        std::byte{0x48}, std::byte{0x89}, std::byte{0xe5},
        std::byte{0x50}};
    std::array<std::byte, 64U> read_only_stack{};
    r::OwnedGuestAddressSpace denied;
    if (!denied.map(0xc00U, before_denial, x.value()).has_value() ||
        !denied.map(0xd00U, read_only_stack, ro.value()).has_value())
        return false;
    e::GuestCpuContext partial_cpu{};
    partial_cpu.rip = 0xc00U;
    partial_cpu.rsp = 0xd20U;
    const auto late_refusal = r::run_owned_x86_64(
        partial_cpu, denied, 4U);
    if (late_refusal.stop != r::OwnedCpuRunStopCode::step_refused ||
        late_refusal.stop_rip != 0xc03U ||
        late_refusal.retired_instructions != 1U ||
        late_refusal.guest_writes != 0U ||
        !late_refusal.step_stop.has_value() ||
        late_refusal.step_stop->code !=
            r::OwnedCpuStepStopCode::guest_memory_refused ||
        !late_refusal.step_stop->memory_fault.has_value() ||
        late_refusal.step_stop->memory_fault->code !=
            r::OwnedGuestMemoryError::permission_denied ||
        partial_cpu.rip != 0xc03U || partial_cpu.rbp != 0xd20U ||
        partial_cpu.rsp != 0xd20U ||
        read_only_stack != std::array<std::byte, 64U>{})
        return false;

    return true;
}

[[nodiscard]] bool test_checked_direct_entry() {
    const auto x = m::GuestPermissions::checked_from_bits(4U);
    const auto rw = m::GuestPermissions::checked_from_bits(3U);
    const auto ro = m::GuestPermissions::checked_from_bits(1U);
    if (!x.has_value() || !rw.has_value() || !ro.has_value())
        return false;
    // AND RSP,-16; CALL rel32 +0x17 to a separately mapped callee
    // at 0x720. No import, Sony startup or native execution.
    std::array<std::byte, 9U> entry{
        std::byte{0x48}, std::byte{0x83}, std::byte{0xe4},
        std::byte{0xf0}, std::byte{0xe8}, std::byte{0x17},
        std::byte{0}, std::byte{0}, std::byte{0}};
    std::array<std::byte, 1U> callee{std::byte{0x55}};
    std::array<std::byte, 128U> stack{};
    r::OwnedGuestAddressSpace memory;
    if (!memory.map(0x700U, entry, x.value()).has_value() ||
        !memory.map(0x720U, callee, x.value()).has_value() ||
        !memory.map(0x1040U, stack, rw.value()).has_value())
        return false;
    e::GuestCpuContext cpu{};
    cpu.rip = 0x700U;
    cpu.rsp = 0x109fU;
    cpu.rflags = 0x813U; // CF, OF and AF initially set
    const auto run = r::run_owned_x86_64(cpu, memory, 2U);
    if (run.stop != r::OwnedCpuRunStopCode::budget_exhausted ||
        run.retired_instructions != 2U || run.guest_writes != 1U ||
        cpu.rip != 0x720U || cpu.rsp != 0x1088U ||
        (cpu.rflags & (1ULL | (1ULL << 11U))) != 0U ||
        (cpu.rflags & ((1ULL << 4U) | (1ULL << 2U))) !=
            ((1ULL << 4U) | (1ULL << 2U)))
        return false;
    std::array<std::byte, 8U> return_word{};
    if (!memory.read(0x1088U, return_word).has_value() ||
        return_word[0] != std::byte{0x09} ||
        return_word[1] != std::byte{0x07} ||
        return_word[7] != std::byte{0})
        return false;

    // The first instruction may retire while a denied later stack write
    // refuses before updating the CPU or guest memory for CALL.
    std::array<std::byte, 128U> readonly_stack{};
    r::OwnedGuestAddressSpace denied;
    if (!denied.map(0x700U, entry, x.value()).has_value() ||
        !denied.map(0x720U, callee, x.value()).has_value() ||
        !denied.map(0x1040U, readonly_stack, ro.value()).has_value())
        return false;
    e::GuestCpuContext no_write{};
    no_write.rip = 0x700U;
    no_write.rsp = 0x109fU;
    const auto refused = r::run_owned_x86_64(no_write, denied, 3U);
    if (refused.stop != r::OwnedCpuRunStopCode::step_refused ||
        refused.retired_instructions != 1U ||
        refused.guest_writes != 0U ||
        refused.stop_rip != 0x704U ||
        !refused.step_stop.has_value() ||
        refused.step_stop->code !=
            r::OwnedCpuStepStopCode::guest_memory_refused ||
        no_write.rip != 0x704U || no_write.rsp != 0x1090U ||
        readonly_stack != std::array<std::byte, 128U>{})
        return false;
    // Missing/nonexecutable CALL destination refuses before stack write.
    std::array<std::byte, 128U> untouched_stack{};
    r::OwnedGuestAddressSpace missing_target;
    if (!missing_target.map(0x700U, entry, x.value()).has_value() ||
        !missing_target.map(0x1040U, untouched_stack, rw.value()).has_value())
        return false;
    e::GuestCpuContext no_target{};
    no_target.rip = 0x704U;
    no_target.rsp = 0x1090U;
    const auto before = no_target;
    const auto direct = record(0x704U,
        {0xe8, 0x17, 0, 0, 0}, ZYDIS_MNEMONIC_CALL);
    const auto wrong = r::step_owned_x86_64(
        direct, no_target, missing_target);
    if (wrong.has_value() ||
        wrong.error().code !=
            r::OwnedCpuStepStopCode::unresolved_control_transfer ||
        no_target != before ||
        untouched_stack != std::array<std::byte, 128U>{})
        return false;
    // An indirect CALL never retires even if its GOT is mapped.
    const auto indirect = record(0x704U,
        {0xff, 0x15, 0x17, 0, 0, 0}, ZYDIS_MNEMONIC_CALL);
    const auto blocked = r::step_owned_x86_64(
        indirect, no_target, missing_target);
    if (blocked.has_value() ||
        blocked.error().code !=
            r::OwnedCpuStepStopCode::unresolved_control_transfer ||
        no_target != before)
        return false;
    return true;
}

} // namespace

int main() {
    if (!test_owned_steps() || !test_owned_fetch_step() ||
        !test_checked_direct_entry()) {
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
    std::puts("owned_guest_fetch_step=bounded_and_typed");
    std::puts("owned_cpu_backend_reference_trace=exact_commits_and_writes");
    std::puts("owned_cpu_backend_stopped_call=no_trace_event");
    std::puts("owned_cpu_backend_short_trace=refused_without_mutation");
    std::puts("owned_fetch_uses_execute_permission=verified");
    std::puts("source_mutation_and_partial_faults=refused");
    std::puts("failed_later_instruction=prior_retirements_only");
    std::puts("owned_direct_entry_and_call=checked_two_instructions");
    std::puts("direct_call_target_and_stack_denials=pre_retirement");
    std::puts("breakout_elf_execution=not_part_of_this_test");
    std::puts("native_hostile_code_containment=not_established");
}
