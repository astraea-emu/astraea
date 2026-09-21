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
    host_allocation_failure,
};

enum class SyntheticSessionEventKind {
    guest_entry,
    gate_stop,
    hle_resume,
    hle_exit,
};

struct SyntheticSessionEvent {
    SyntheticSessionEventKind kind =
        SyntheticSessionEventKind::guest_entry;
    std::uint64_t rip = 0;
    std::uint64_t rsp = 0;
    bool has_gate_slot = false;
    std::uint32_t gate_slot = 0;
    bool has_function_id = false;
    HleFunctionId function_id;
    std::uint64_t value = 0;

    auto operator<=>(const SyntheticSessionEvent&) const = default;
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
    std::vector<SyntheticSessionEvent> events;

    auto operator<=>(const LinuxSyntheticSessionResult&) const = default;
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
