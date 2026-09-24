#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <astraea/loader/guest_image.hpp>
#include <astraea/loader/initial_stack.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

enum class LinuxRetailDiagnosticPreflightBoundaryKind {
    loader_rejected,
    entry_not_executable,
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
//   3. unresolved dynamic/module dependencies
//   4. unapplied dynamic relocations
//   5. TLS runtime setup required
//   6. ready for the Linux seccomp-protected native-entry stage
//
// This function never executes guest instructions, performs host filesystem
// access, or resolves/imports modules. It is a diagnostic admission planner,
// not a compatibility claim.
[[nodiscard]] LinuxRetailDiagnosticPreflight
preflight_linux_retail_diagnostic(
    LinuxRetailDiagnosticPreflightRequest request);

}  // namespace astraea::execution
