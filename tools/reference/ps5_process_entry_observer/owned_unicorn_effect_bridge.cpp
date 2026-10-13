// Research-only consumer of independently executed, fixed Unicorn effects.
// The transcript is untrusted, not a process ABI or a post-commit proof.
// This verifier checks exact source-owned expectations, replays proven final
// guest bytes into a separate owned memory model, and exercises the ledger.
#include "owned_cpu_effect_ledger.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace r = astraea::research;
namespace e = astraea::execution;
namespace m = astraea::memory;

constexpr std::uint64_t kCode = 0x100000U;
constexpr std::uint64_t kData = 0x300000U;
constexpr std::uint64_t kStack = 0x400000U;
constexpr std::size_t kPage = 4096U;
constexpr std::uint64_t kSource = kStack + 0x100U;

[[nodiscard]] bool hex(std::string_view s, std::uint64_t& value) {
    if (s.empty() || s.size() > 16U) return false;
    for (char ch : s) {
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f')))
            return false;
    }
    const auto [last, error] = std::from_chars(
        s.data(), s.data() + s.size(), value, 16);
    return error == std::errc{} && last == s.data() + s.size();
}

[[nodiscard]] bool parse_cpu(std::string_view line,
                             std::string_view label,
                             e::GuestCpuContext& cpu) {
    if (!line.starts_with(label) ||
        line.size() <= label.size() || line[label.size()] != '|')
        return false;
    line.remove_prefix(label.size() + 1U);
    std::array<std::uint64_t, 20U> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto separator = line.find(',');
        if ((i + 1U < values.size() && separator == line.npos) ||
            (i + 1U == values.size() && separator != line.npos))
            return false;
        const auto field = line.substr(0, separator);
        if (field.size() != 16U || !hex(field, values[i])) return false;
        if (i + 1U < values.size()) line.remove_prefix(separator + 1U);
    }
    cpu.rax = values[0]; cpu.rbx = values[1];
    cpu.rcx = values[2]; cpu.rdx = values[3];
    cpu.rsi = values[4]; cpu.rdi = values[5];
    cpu.rbp = values[6]; cpu.rsp = values[7];
    cpu.r8 = values[8]; cpu.r9 = values[9];
    cpu.r10 = values[10]; cpu.r11 = values[11];
    cpu.r12 = values[12]; cpu.r13 = values[13];
    cpu.r14 = values[14]; cpu.r15 = values[15];
    cpu.rip = values[16]; cpu.rflags = values[17];
    cpu.fs_base = values[18]; cpu.gs_base = values[19];
    return true;
}

[[nodiscard]] bool mapping(
    r::OwnedGuestAddressSpace& memory, std::uint64_t address,
    std::span<std::byte> storage, std::uint8_t bits) {
    const auto perms = m::GuestPermissions::checked_from_bits(bits);
    return perms.has_value() &&
           memory.map(address, storage, perms.value()).has_value();
}

[[nodiscard]] bool verify(const std::string& which, const char* filepath) {
    const bool complete = which == "complete";
    if (!complete && which != "partial") return false;

    std::ifstream input(filepath, std::ios::binary);
    if (!input) return false;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (line.size() > 512U || lines.size() >= 12U ||
            line.find('\r') != std::string::npos)
            return false;
        lines.push_back(line);
    }
    if (!input.eof()) return false;

    const std::size_t commits = complete ? 4U : 1U;
    const std::string header = "C1EFFECT|1|" + which + "|f3a4";
    if (lines.size() != 5U + commits ||
        lines[0] != header || lines.back() != "END")
        return false;

    e::GuestCpuContext before{}, after{};
    if (!parse_cpu(lines[1], "BEFORE", before) ||
        !parse_cpu(lines[2], "AFTER", after))
        return false;

    const std::uint64_t destination =
        kData + (complete ? 0x80U : kPage - 1U);
    const std::array<std::byte, 4U> expected_source{
        std::byte{0x11}, std::byte{0x22},
        std::byte{0x33}, std::byte{0x44}};
    const std::array<std::byte, 3U> partial_source{
        std::byte{0xa5}, std::byte{0xb6}, std::byte{0xc7}};
    const std::size_t total = complete ? 4U : 3U;

    e::GuestCpuContext expected_before{};
    expected_before.rip = kCode;
    expected_before.rsp = kStack + 0x800U;
    expected_before.rflags = 2U;
    expected_before.rsi = kSource;
    expected_before.rdi = destination;
    expected_before.rcx = total;
    if (before != expected_before) return false;
    auto expected_after = expected_before;
    expected_after.rip = complete ? kCode + 2U : kCode;
    expected_after.rsi += commits;
    expected_after.rdi += commits;
    expected_after.rcx -= commits;
    if (after != expected_after) return false;

    const auto exact_outcome = complete ?
        "OUTCOME|retired|none|-" :
        "OUTCOME|fault_after_effects|memory_permission|301000";
    if (lines[3] != exact_outcome) return false;

    std::array<std::byte, 2U> code{
        std::byte{0xf3}, std::byte{0xa4}};
    std::array<std::byte, kPage> data{};
    std::array<std::byte, kPage> readonly{};
    std::array<std::byte, kPage> stack{};
    readonly.fill(std::byte{0x77});
    for (std::size_t i = 0; i < total; ++i)
        stack[0x100U + i] = complete ?
            expected_source[i] : partial_source[i];

    r::OwnedGuestAddressSpace memory;
    if (!mapping(memory, kCode, code, 5U) ||
        !mapping(memory, kData, data, 3U) ||
        !mapping(memory, kData + kPage, readonly, 1U) ||
        !mapping(memory, kStack, stack, 3U))
        return false;
    r::OwnedCpuEffectLedger ledger;
    if (!ledger.begin(before)) return false;

    for (std::size_t i = 0; i < commits; ++i) {
        const std::string& event = lines[4U + i];
        if (!event.starts_with("WRITE|")) return false;
        const std::string_view fields(event);
        const auto delimiter = fields.find('|', 6U);
        if (delimiter == fields.npos) return false;
        const auto addr_hex = fields.substr(6U, delimiter - 6U);
        const auto byte_hex = fields.substr(delimiter + 1U);
        std::uint64_t address = 0U, value = 0U;
        if (byte_hex.size() != 2U ||
            !hex(addr_hex, address) || !hex(byte_hex, value) ||
            address != destination + i || value > 255U ||
            value != static_cast<std::uint64_t>(
                std::to_integer<unsigned>(complete ?
                    expected_source[i] : partial_source[i])))
            return false;
        const std::array<std::byte, 1U> payload{
            static_cast<std::byte>(value)};
        if (!memory.write(address, payload).has_value() ||
            !ledger.append_completed_write(address, payload, memory))
            return false;
    }

    r::OwnedCpuEffectReport report{};
    const auto outcome = complete ?
        r::OwnedEffectOutcome::retired :
        r::OwnedEffectOutcome::fault_after_effects;
    const auto fault = complete ?
        r::OwnedEffectFault::none :
        r::OwnedEffectFault::memory_permission;
    const std::optional<std::uint64_t> fault_address =
        complete ? std::nullopt :
        std::optional<std::uint64_t>{kData + kPage};
    if (!ledger.finish(outcome, after, fault, fault_address, report) ||
        report.write_count != commits ||
        report.before_context != before ||
        report.after_context != after ||
        report.outcome != outcome ||
        report.fault != fault ||
        report.fault_address != fault_address)
        return false;

    // Verify final bytes in independent guest-owned buffers; no native
    // guest mapping, Sony module, ABI or host Unicorn linkage exists here.
    for (std::size_t i = 0; i < commits; ++i) {
        if (data[static_cast<std::size_t>(destination - kData) + i] !=
            (complete ? expected_source[i] : partial_source[i]))
            return false;
    }
    for (const auto b : readonly) {
        if (b != std::byte{0x77}) return false;
    }
    for (std::size_t i = 0; i < total; ++i) {
        if (stack[0x100U + i] !=
            (complete ? expected_source[i] : partial_source[i]))
            return false;
    }

    std::puts("C1 EXTERNAL UNICORN TO OWNED EFFECT LEDGER PASS");
    std::printf("case=%s\n", which.c_str());
    std::printf("verified_guest_writes=%zu\n", report.write_count);
    std::puts("complete_context=checked");
    std::puts("fault_stage_and_restart_state=checked");
    std::puts("source_owned_transcript=exact_frozen_fixture");
    std::puts("external_engine_linked_into_core=no");
    std::puts("sony_services=not_executed");
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3 || !verify(argv[1], argv[2])) {
        std::fputs("C1 EXTERNAL UNICORN EFFECT LEDGER BRIDGE REFUSED\n", stderr);
        return 1;
    }
    return 0;
}
