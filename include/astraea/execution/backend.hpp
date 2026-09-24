#pragma once

#include <compare>
#include <cstdint>

namespace astraea::execution {

enum class NativeBackendErrorCode {
    backend_unavailable,
    unsupported_host_architecture,
    guest_address_unavailable,
    host_page_arithmetic_overflow,
    host_mapping_failure,
    host_protection_failure,
    instruction_cache_sync_failure,
    mixed_write_execute_page,
    stack_mapping_failure,
    entry_point_unmapped,
    entry_point_not_executable,
    stack_pointer_unmapped,
    tls_runtime_layout_unsupported,
    nested_execution_unsupported,
    recovery_setup_failure,
    invalid_guest_context,
    invalid_registered_syscall_trap,
    syscall_interception_setup_failure,
    internal_transition_failure,
};

struct NativeBackendError {
    NativeBackendErrorCode code = NativeBackendErrorCode::internal_transition_failure;
    bool has_guest_address = false;
    std::uint64_t guest_address = 0;
    bool has_host_code = false;
    std::uint64_t host_code = 0;

    auto operator<=>(const NativeBackendError&) const = default;
};

}  // namespace astraea::execution
