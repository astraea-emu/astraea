#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::execution {

inline constexpr int kLinuxWorkerArtifactFd = 3;

enum class LinuxWorkerArtifactErrorCode {
    unsupported_platform,
    invalid_size_limit,
    descriptor_unavailable,
    seal_query_failure,
    missing_required_seals,
    metadata_failure,
    empty_artifact,
    artifact_too_large,
    read_failure,
    close_failure,
    host_allocation_failure,
};

struct LinuxWorkerArtifactError {
    LinuxWorkerArtifactErrorCode code =
        LinuxWorkerArtifactErrorCode::
            descriptor_unavailable;
    std::int64_t platform_error = 0;
    std::uint64_t observed_size = 0;

    auto operator<=>(const LinuxWorkerArtifactError&) const =
        default;
};

using LinuxWorkerArtifactReadResult =
    astraea::core::Result<
        std::vector<std::byte>,
        LinuxWorkerArtifactError>;

// Consumes the fixed Linux worker artifact descriptor.
//
// The caller must provide a finite non-zero allocation ceiling. On Linux this
// function requires the complete immutable seal set, validates regular-file
// metadata and size before allocation, copies the exact bytes with pread(), and
// closes fd 3 before returning success.
//
// The descriptor is best-effort closed on every failure path as well. This
// helper accepts no pathname and performs no guest filesystem access.
[[nodiscard]] LinuxWorkerArtifactReadResult
read_linux_sealed_worker_artifact(
    std::size_t max_artifact_bytes);

}  // namespace astraea::execution
