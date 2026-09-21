#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::probe {

enum class ReferenceProbeErrorCode {
    host_size_unrepresentable,
    host_allocation_failure,
};

struct ReferenceProbeError {
    ReferenceProbeErrorCode code =
        ReferenceProbeErrorCode::host_allocation_failure;

    auto operator<=>(const ReferenceProbeError&) const = default;
};

struct ReferenceEchoRequest {
    std::vector<std::byte> message;
    std::uint64_t exit_code = 0;

    auto operator<=>(const ReferenceEchoRequest&) const = default;
};

struct ReferenceEchoResult {
    std::vector<std::byte> output;
    std::uint64_t bytes_consumed = 0;
    std::uint64_t exit_code = 0;

    auto operator<=>(const ReferenceEchoResult&) const = default;
};

using ReferenceEchoRunResult =
    astraea::core::Result<
        ReferenceEchoResult,
        ReferenceProbeError>;

[[nodiscard]] ReferenceEchoRunResult run_reference_echo(
    const ReferenceEchoRequest& request);

}  // namespace astraea::probe
