#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::app {

inline constexpr std::size_t kMaxRetailArtifactBytes =
    1024ULL * 1024ULL * 1024ULL;

enum class ArtifactReadErrorCode {
    open_failed,
    size_query_failed,
    empty_artifact,
    artifact_too_large,
    read_failed,
    artifact_changed_during_read,
    host_allocation_failure,
};

using ArtifactReadResult =
    astraea::core::Result<
        std::vector<std::byte>,
        ArtifactReadErrorCode>;

[[nodiscard]] std::string_view
artifact_read_error_name(
    ArtifactReadErrorCode code) noexcept;

// Reads through one already-opened file object so path replacement/growth
// cannot silently turn analysis into a different or truncated artifact.
[[nodiscard]] ArtifactReadResult
read_artifact_file(
    std::string_view artifact_path,
    std::size_t maximum_bytes = kMaxRetailArtifactBytes);

}  // namespace astraea::app
