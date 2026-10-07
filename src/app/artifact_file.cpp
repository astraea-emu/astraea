#include "artifact_file.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace astraea::app {

std::string_view
artifact_read_error_name(
    ArtifactReadErrorCode code) noexcept {
    switch (code) {
    case ArtifactReadErrorCode::open_failed:
        return "open_failed";
    case ArtifactReadErrorCode::size_query_failed:
        return "size_query_failed";
    case ArtifactReadErrorCode::empty_artifact:
        return "empty_artifact";
    case ArtifactReadErrorCode::artifact_too_large:
        return "artifact_too_large";
    case ArtifactReadErrorCode::read_failed:
        return "read_failed";
    case ArtifactReadErrorCode::artifact_changed_during_read:
        return "artifact_changed_during_read";
    case ArtifactReadErrorCode::host_allocation_failure:
        return "host_allocation_failure";
    }
    return "unknown";
}

ArtifactReadResult
read_artifact_file(
    std::string_view artifact_path,
    std::size_t maximum_bytes) {
    try {
        const std::filesystem::path path{
            std::string{artifact_path}};

        std::ifstream input(
            path,
            std::ios::binary | std::ios::ate);
        if (!input) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::open_failed);
        }

        const auto end = input.tellg();
        if (end < std::streampos{0}) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::size_query_failed);
        }
        if (end == std::streampos{0}) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::empty_artifact);
        }

        const auto byte_count =
            static_cast<std::uintmax_t>(
                static_cast<std::streamoff>(end));
        if (byte_count >
                static_cast<std::uintmax_t>(maximum_bytes) ||
            byte_count >
                static_cast<std::uintmax_t>(
                    std::numeric_limits<std::size_t>::max()) ||
            byte_count >
                static_cast<std::uintmax_t>(
                    std::numeric_limits<std::streamsize>::max())) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::artifact_too_large);
        }

        input.seekg(0, std::ios::beg);
        if (!input) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::read_failed);
        }

        const auto host_size =
            static_cast<std::size_t>(byte_count);
        std::vector<std::byte> bytes(host_size);
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(host_size));
        if (!input ||
            static_cast<std::size_t>(input.gcount()) !=
                host_size) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::read_failed);
        }

        char trailing = '\0';
        input.read(&trailing, 1);
        if (input.gcount() != 0) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::
                    artifact_changed_during_read);
        }

        return ArtifactReadResult::success(
            std::move(bytes));
    } catch (const std::bad_alloc&) {
        return ArtifactReadResult::failure(
            ArtifactReadErrorCode::
                host_allocation_failure);
    } catch (const std::length_error&) {
        return ArtifactReadResult::failure(
            ArtifactReadErrorCode::
                host_allocation_failure);
    }
}

}  // namespace astraea::app
