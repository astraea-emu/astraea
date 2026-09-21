#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::loader {

struct AuxiliaryVectorEntry {
    std::uint64_t type;
    std::uint64_t value;

    auto operator<=>(const AuxiliaryVectorEntry&) const = default;
};

struct InitialStackRequest {
    astraea::memory::GuestRange storage;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::vector<AuxiliaryVectorEntry> auxiliary_vector;
};

enum class InitialStackErrorCode {
    embedded_nul,
    auxv_contains_terminator,
    layout_size_overflow,
    stack_too_small,
    host_size_unrepresentable,
    guest_address_overflow,
    host_allocation_failure,
};

enum class InitialStackInputKind {
    argument,
    environment,
    auxiliary_vector,
};

struct InitialStackError {
    InitialStackErrorCode code;
    std::optional<InitialStackInputKind> input_kind;
    std::optional<std::size_t> input_index;
};

struct InitialStackImage {
    astraea::memory::GuestRange storage;
    astraea::memory::GuestRange used_range;
    astraea::memory::GuestAddress rsp;
    std::vector<std::byte> bytes;
};

using InitialStackResult =
    astraea::core::Result<InitialStackImage, InitialStackError>;

[[nodiscard]] InitialStackResult build_initial_stack(
    const InitialStackRequest& request);

}  // namespace astraea::loader
