#pragma once

#include <compare>
#include <cstdint>
#include <span>
#include <variant>

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

struct LinuxSeccompSyscallTrap {
    GuestCpuContext context;
    astraea::memory::GuestAddress guest_rip;
    std::int32_t syscall_number = 0;
    std::uint32_t audit_arch = 0;

    auto operator<=>(const LinuxSeccompSyscallTrap&) const = default;
};

using LinuxSeccompExecutionEvent =
    std::variant<
        ExecutionStop,
        LinuxSeccompSyscallTrap>;

using LinuxSeccompExecutionResult =
    astraea::core::Result<
        LinuxSeccompExecutionEvent,
        NativeBackendError>;

[[nodiscard]] bool linux_native_execution_backend_available() noexcept;

[[nodiscard]] bool
linux_guest_syscall_seccomp_available() noexcept;

[[nodiscard]] LinuxExecutionResult enter_linux_guest(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps = {});

// Executes with a Linux seccomp filter on the existing dedicated native
// execution thread. Any syscall whose kernel-reported instruction pointer lies
// inside an exact executable GuestImage mapping is trapped before execution.
[[nodiscard]] LinuxSeccompExecutionResult
enter_linux_guest_with_seccomp_syscall_trap(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context);

}  // namespace astraea::execution
