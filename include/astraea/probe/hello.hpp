#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/initial_stack.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::probe {

inline constexpr std::string_view kProbeHelloMessage =
    "Hello from guest";

enum class ProbeHelloErrorCode {
    invalid_page_size,
    unaligned_guest_base,
    guest_address_overflow,
    relative_call_out_of_range,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct ProbeHelloError {
    ProbeHelloErrorCode code =
        ProbeHelloErrorCode::host_allocation_failure;
    bool has_guest_address = false;
    std::uint64_t guest_address = 0;

    auto operator<=>(const ProbeHelloError&) const = default;
};

struct ProbeHelloFixture {
    std::vector<std::byte> elf_bytes;
    astraea::loader::InitialStackRequest initial_stack;
    astraea::memory::GuestAddress code_base;
    astraea::memory::GuestAddress data_base;
    astraea::memory::GuestAddress gate_base;
};

using ProbeHelloResult =
    astraea::core::Result<
        ProbeHelloFixture,
        ProbeHelloError>;

[[nodiscard]] ProbeHelloResult build_probe_hello_fixture(
    astraea::memory::GuestAddress guest_base,
    std::uint64_t page_size);

}  // namespace astraea::probe
