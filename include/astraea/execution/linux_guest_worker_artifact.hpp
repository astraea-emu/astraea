#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::execution {

inline constexpr int kLinuxGuestWorkerArtifactFd = 3;

enum class LinuxGuestWorkerArtifactErrorCode {
    unsupported_platform,
    invalid_fd,
    seal_query_failure,
    missing_required_seal,
    stat_failure,
    empty_artifact,
    artifact_too_large,
    host_size_unrepresentable,
    read_failure,
    unexpected_eof,
    host_allocation_failure,
};

struct LinuxGuestWorkerArtifactError {
    LinuxGuestWorkerArtifactErrorCode code =
        LinuxGuestWorkerArtifactErrorCode::
            unsupported_platform;
    std::int64_t platform_error = 0;
    std::uint64_t actual_value = 0;

    auto operator<=>(const LinuxGuestWorkerArtifactError&) const =
        default;
};

using LinuxGuestWorkerArtifactLoadResult =
    astraea::core::Result<
        std::vector<std::byte>,
        LinuxGuestWorkerArtifactError>;

// Loads the controller-admitted artifact from the fixed inherited descriptor.
//
// Linux requires all four immutable seals before any bytes are trusted:
// F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL.
//
// The descriptor is not closed by this helper; the worker closes it after
// materializing the bytes and before guest RUN.
[[nodiscard]] LinuxGuestWorkerArtifactLoadResult
load_linux_guest_worker_artifact(
    int fd,
    std::uint64_t max_artifact_bytes);

}  // namespace astraea::execution
