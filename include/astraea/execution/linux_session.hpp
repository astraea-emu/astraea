#pragma once

#include <compare>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/backend.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_execution.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::execution {

enum class LinuxSyntheticSessionErrorKind {
    backend,
    hle,
    guest_fault,
    unexpected_stop,
};

struct LinuxSyntheticSessionError {
    LinuxSyntheticSessionErrorKind kind =
        LinuxSyntheticSessionErrorKind::unexpected_stop;
    NativeBackendError backend_error;
    HleRuntimeError hle_error;
    GuestFault guest_fault;

    auto operator<=>(const LinuxSyntheticSessionError&) const = default;
};

struct LinuxSyntheticSessionResult {
    std::uint64_t exit_code = 0;
    std::uint64_t gate_stop_count = 0;
    GuestCpuContext final_context;
    std::vector<std::byte> output;
};

using LinuxSyntheticSessionRunResult =
    astraea::core::Result<
        LinuxSyntheticSessionResult,
        LinuxSyntheticSessionError>;

[[nodiscard]] LinuxSyntheticSessionRunResult
run_linux_synthetic_session(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const HleRegistry& registry,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext initial_context);

}  // namespace astraea::execution
