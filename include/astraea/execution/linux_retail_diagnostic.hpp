#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/backend.hpp>
#include <astraea/execution/context.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/loader/initial_stack.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

enum class LinuxRetailDiagnosticStackErrorCode {
    elf_parse_failure,
    load_range_overflow,
    address_space_exhausted,
};

struct LinuxRetailDiagnosticStackError {
    LinuxRetailDiagnosticStackErrorCode code =
        LinuxRetailDiagnosticStackErrorCode::
            elf_parse_failure;
    std::optional<astraea::loader::ElfError> elf_error;
    std::uint64_t detail = 0;

    auto operator<=>(const LinuxRetailDiagnosticStackError&) const =
        default;
};

using LinuxRetailDiagnosticStackResult =
    astraea::core::Result<
        astraea::memory::GuestRange,
        LinuxRetailDiagnosticStackError>;

inline constexpr std::uint64_t
    kLinuxRetailDiagnosticStackSize = 2U * 1024U * 1024U;
inline constexpr std::uint64_t
    kLinuxRetailDiagnosticStackGuard = 64U * 1024U;
inline constexpr std::uint64_t
    kLinuxRetailDiagnosticStackAlignment = 64U * 1024U;

// Deterministic first-diagnostic stack policy. The stack is placed after the
// highest PT_LOAD range, with a fixed guard and alignment, and must remain in
// the conservative lower canonical x86-64 user half.
[[nodiscard]] LinuxRetailDiagnosticStackResult
choose_linux_retail_diagnostic_stack(
    std::span<const std::byte> artifact_bytes);

enum class LinuxRetailDiagnosticRuntimeBoundaryKind {
    unsupported_syscall,
    guest_fault,
    native_backend_error,
};

struct LinuxRetailDiagnosticRuntimeBoundary {
    LinuxRetailDiagnosticRuntimeBoundaryKind kind =
        LinuxRetailDiagnosticRuntimeBoundaryKind::
            native_backend_error;
    astraea::memory::GuestAddress guest_rip{0U};
    std::int32_t syscall_number = 0;
    std::uint32_t audit_arch = 0;
    std::optional<GuestFault> guest_fault;
    std::optional<NativeBackendError> backend_error;

    auto operator<=>(const LinuxRetailDiagnosticRuntimeBoundary&) const =
        default;
};

// Executes exactly one diagnostic native-entry interval under the verified
// Linux seccomp syscall boundary, with no synthetic HLE gate. The caller must
// have admitted the image through preflight first. This function never brokers
// a guest syscall or resumes after a syscall/fault boundary.
[[nodiscard]] LinuxRetailDiagnosticRuntimeBoundary
run_linux_retail_diagnostic_native_once(
    const astraea::loader::GuestImage& image);

enum class LinuxRetailDiagnosticPreflightBoundaryKind {
    loader_rejected,
    entry_not_executable,
    sce_dynamic_metadata_rejected,
    unsupported_dynamic_dependencies,
    unsupported_relocations,
    unsupported_tls,
    ready_for_native_entry,
};

struct LinuxRetailDiagnosticPreflightRequest {
    std::vector<std::byte> artifact_bytes;
    astraea::memory::GuestRange stack_storage;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::vector<astraea::loader::AuxiliaryVectorEntry>
        auxiliary_vector;
};

struct LinuxRetailDiagnosticPreflight {
    LinuxRetailDiagnosticPreflightBoundaryKind boundary =
        LinuxRetailDiagnosticPreflightBoundaryKind::
            loader_rejected;

    // Present only for loader_rejected.
    std::optional<astraea::loader::GuestImageError>
        loader_error;

    // Present only for sce_dynamic_metadata_rejected.
    std::optional<astraea::loader::SceDynamicMetadataError>
        sce_dynamic_metadata_error;

    // Deterministic detail counts for unsupported pre-entry work.
    std::size_t dynamic_dependency_count = 0;
    std::uint64_t relocation_count = 0;

    // Present for every successfully built image. Native entry is permitted
    // only when boundary == ready_for_native_entry.
    std::optional<astraea::loader::GuestImage> image;
};

// Builds a PS5/SCE GuestImage from already-authorized bytes and reports the
// first known pre-entry runtime boundary.
//
// Ordering is deliberate:
//   1. loader structural rejection
//   2. entry point is not executable
//   3. SCE dynamic metadata cannot be represented faithfully
//   4. unresolved dynamic/module dependencies
//   5. unapplied dynamic relocations
//   6. TLS runtime setup required
//   7. ready for the Linux seccomp-protected native-entry stage
//
// This function never executes guest instructions, performs host filesystem
// access, or resolves/imports modules. It is a diagnostic admission planner,
// not a compatibility claim.
[[nodiscard]] LinuxRetailDiagnosticPreflight
preflight_linux_retail_diagnostic(
    LinuxRetailDiagnosticPreflightRequest request);

}  // namespace astraea::execution
