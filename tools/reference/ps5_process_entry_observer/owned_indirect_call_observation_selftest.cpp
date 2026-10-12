// Independent source-owned test: an indirect call is OBSERVED, not retired.
// No external ELF, Sony HLE, native guest entry, or host pointer dereference.
#include "owned_indirect_call_observation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <Zydis/Zydis.h>

namespace {
namespace r = astraea::research;
namespace m = astraea::memory;
namespace e = astraea::execution;
using Error = r::OwnedIndirectCallErrorCode;

[[nodiscard]] r::DecodedSourceInstruction record(
    std::uint64_t rip, std::array<std::byte, 6U> bytes,
    ZydisMnemonic mnemonic = ZYDIS_MNEMONIC_CALL) {
    r::DecodedSourceInstruction rec{};
    rec.rip = rip;
    rec.length = 6U;
    rec.mnemonic = static_cast<std::uint32_t>(mnemonic);
    for (std::size_t i = 0U; i < bytes.size(); ++i) rec.bytes[i] = bytes[i];
    return rec;
}

[[nodiscard]] bool conformance() {
    const auto execute = m::GuestPermissions::checked_from_bits(4U);
    const auto read = m::GuestPermissions::checked_from_bits(1U);
    const auto write = m::GuestPermissions::checked_from_bits(2U);
    if (!execute.has_value() || !read.has_value() ||
        !write.has_value()) return false;

    // CALL qword ptr [RIP+8] at 0x400. Next RIP = 0x406;
    // GOT slot = 0x40e, source-owned value 0x1122334455667788.
    std::array<std::byte, 6U> code{
        std::byte{0xff}, std::byte{0x15}, std::byte{0x08},
        std::byte{0}, std::byte{0}, std::byte{0}};
    std::array<std::byte, 8U> got{
        std::byte{0x88}, std::byte{0x77}, std::byte{0x66},
        std::byte{0x55}, std::byte{0x44}, std::byte{0x33},
        std::byte{0x22}, std::byte{0x11}};
    r::OwnedGuestAddressSpace memory;
    if (!memory.map(0x400U, code, execute.value()).has_value() ||
        !memory.map(0x40eU, got, read.value()).has_value())
        return false;
    e::GuestCpuContext cpu{};
    cpu.rip = 0x400U;
    cpu.rsp = 0xdeadbeefU;
    cpu.rflags = 0x202U;
    const auto before_cpu = cpu;
    const auto before_code = code;
    const auto before_got = got;
    const auto call = record(0x400U, code);
    const auto observed = r::observe_owned_indirect_call(call, cpu, memory);
    if (!observed.has_value() ||
        observed->call_rip != 0x400U ||
        observed->return_rip != 0x406U ||
        observed->pointer_guest_address != 0x40eU ||
        observed->pointer_word_bytes != got ||
        observed->pointer_word_le != 0x1122334455667788ULL ||
        cpu != before_cpu || code != before_code || got != before_got)
        return false;

    // Changing the caller record (or the staged executable) cannot
    // manufacture an observed CALL; neither mutation alters CPU state.
    auto wrong_record = call;
    wrong_record.bytes[0] = std::byte{0x90};
    const auto stale_record = r::observe_owned_indirect_call(
        wrong_record, cpu, memory);
    if (stale_record.has_value() ||
        stale_record.error().code != Error::source_bytes_mismatch)
        return false;
    code[0] = std::byte{0x90};
    const auto stale_image = r::observe_owned_indirect_call(call, cpu, memory);
    if (stale_image.has_value() ||
        stale_image.error().code != Error::source_bytes_mismatch)
        return false;
    code = before_code;

    // Wrong context, false mnemonic, invalid instruction form refuse.
    auto wrong_cpu = cpu;
    wrong_cpu.rip = 0x401U;
    const auto context = r::observe_owned_indirect_call(
        call, wrong_cpu, memory);
    if (context.has_value() ||
        context.error().code != Error::context_mismatch)
        return false;
    auto false_mnemonic = call;
    false_mnemonic.mnemonic = static_cast<std::uint32_t>(
        ZYDIS_MNEMONIC_MOV);
    const auto malformed = r::observe_owned_indirect_call(
        false_mnemonic, cpu, memory);
    if (malformed.has_value() ||
        malformed.error().code != Error::invalid_encoding)
        return false;

    // A register-indirect CALL is not the admitted RIP-relative form.
    std::array<std::byte, 6U> reg_code{
        std::byte{0xff}, std::byte{0xd0}, std::byte{0x90},
        std::byte{0x90}, std::byte{0x90}, std::byte{0x90}};
    auto reg_record = record(0x500U, reg_code);
    reg_record.length = 2U;
    r::OwnedGuestAddressSpace register_memory;
    if (!register_memory.map(
            0x500U, reg_code, execute.value()).has_value())
        return false;
    e::GuestCpuContext reg_cpu{};
    reg_cpu.rip = 0x500U;
    const auto unsupported = r::observe_owned_indirect_call(
        reg_record, reg_cpu, register_memory);
    if (unsupported.has_value() ||
        unsupported.error().code != Error::unsupported_call_form)
        return false;

    // Guest execute permission is mandatory for the CALL bytes.
    r::OwnedGuestAddressSpace nonexec;
    if (!nonexec.map(0x400U, code, read.value()).has_value() ||
        !nonexec.map(0x40eU, got, read.value()).has_value())
        return false;
    const auto no_fetch = r::observe_owned_indirect_call(
        call, cpu, nonexec);
    if (no_fetch.has_value() ||
        no_fetch.error().code != Error::source_fetch_refused ||
        !no_fetch.error().memory_fault.has_value() ||
        no_fetch.error().memory_fault->code !=
            r::OwnedGuestMemoryError::permission_denied)
        return false;

    // The GOT operand is fetched with *read*, not execute permission.
    r::OwnedGuestAddressSpace denied;
    if (!denied.map(0x400U, code, execute.value()).has_value() ||
        !denied.map(0x40eU, got, write.value()).has_value())
        return false;
    const auto no_read = r::observe_owned_indirect_call(
        call, cpu, denied);
    if (no_read.has_value() ||
        no_read.error().code != Error::operand_read_refused ||
        !no_read.error().memory_fault.has_value() ||
        no_read.error().memory_fault->code !=
            r::OwnedGuestMemoryError::permission_denied)
        return false;

    // A missing/truncated GOT cannot partially read a pointer.
    r::OwnedGuestAddressSpace unmapped;
    if (!unmapped.map(0x400U, code, execute.value()).has_value())
        return false;
    const auto no_got = r::observe_owned_indirect_call(
        call, cpu, unmapped);
    if (no_got.has_value() ||
        no_got.error().code != Error::operand_read_refused ||
        !no_got.error().memory_fault.has_value() ||
        no_got.error().memory_fault->code !=
            r::OwnedGuestMemoryError::unmapped)
        return false;
    std::array<std::byte, 4U> truncated{};
    r::OwnedGuestAddressSpace short_got;
    if (!short_got.map(0x400U, code, execute.value()).has_value() ||
        !short_got.map(0x40eU, truncated, read.value()).has_value())
        return false;
    const auto no_partial = r::observe_owned_indirect_call(
        call, cpu, short_got);
    if (no_partial.has_value() ||
        no_partial.error().code != Error::operand_read_refused)
        return false;

    // Failed/valid observations must not retire CALL or touch memory.
    if (cpu != before_cpu || code != before_code || got != before_got)
        return false;
    return true;
}
} // namespace

int main() {
    if (!conformance()) {
        std::fputs("C1 INDIRECT CALL OBSERVATION REFUSED\n", stderr);
        return 1;
    }
    std::puts("C1 INDIRECT CALL OBSERVATION PASS");
    std::puts("rip_relative_got_word=read_via_guest_virtual_memory");
    std::puts("call_control_transfer=not_retired");
    std::puts("source_byte_and_context_mismatch=refused");
    std::puts("nonexec_and_unreadable_got=refused");
    std::puts("missing_and_truncated_got=refused");
    std::puts("cpu_and_guest_bytes=unchanged");
    std::puts("sony_provider_resolution=not_attempted");
    std::puts("native_guest_execution=none");
}
