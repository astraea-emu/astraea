#pragma once

#include <span>

#include <astraea/core/result.hpp>
#include <astraea/execution/backend.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/registered_syscall_trap.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::execution {

using LinuxExecutionResult =
    astraea::core::Result<ExecutionStop, NativeBackendError>;

[[nodiscard]] bool linux_native_execution_backend_available() noexcept;

[[nodiscard]] LinuxExecutionResult enter_linux_guest(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps = {});

}  // namespace astraea::execution
