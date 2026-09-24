#include <astraea/execution/linux_guest_worker_artifact.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace astraea::execution {
namespace {

[[nodiscard]] LinuxGuestWorkerArtifactError error(
    LinuxGuestWorkerArtifactErrorCode code,
    std::int64_t platform_error = 0,
    std::uint64_t actual_value = 0) noexcept {
    return LinuxGuestWorkerArtifactError{
        .code = code,
        .platform_error = platform_error,
        .actual_value = actual_value,
    };
}

}  // namespace

LinuxGuestWorkerArtifactLoadResult
load_linux_guest_worker_artifact(
    int fd,
    std::uint64_t max_artifact_bytes) {
#if !defined(__linux__)
    (void)fd;
    (void)max_artifact_bytes;
    return LinuxGuestWorkerArtifactLoadResult::failure(
        error(
            LinuxGuestWorkerArtifactErrorCode::
                unsupported_platform));
#else
    if (fd < 0 || max_artifact_bytes == 0U) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    invalid_fd));
    }

    errno = 0;
    const auto seals =
        ::fcntl(
            fd,
            F_GET_SEALS);
    if (seals < 0) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    seal_query_failure,
                errno));
    }

    constexpr int kRequiredSeals =
        F_SEAL_WRITE |
        F_SEAL_GROW |
        F_SEAL_SHRINK |
        F_SEAL_SEAL;
    if ((seals & kRequiredSeals) !=
        kRequiredSeals) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    missing_required_seal,
                0,
                static_cast<std::uint64_t>(
                    static_cast<unsigned int>(
                        seals))));
    }

    struct stat metadata {};
    errno = 0;
    if (::fstat(
            fd,
            &metadata) != 0) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    stat_failure,
                errno));
    }

    if (metadata.st_size <= 0) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    empty_artifact));
    }

    const auto size =
        static_cast<std::uint64_t>(
            metadata.st_size);
    if (size > max_artifact_bytes) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    artifact_too_large,
                0,
                size));
    }

    if constexpr (
        sizeof(std::size_t) <
        sizeof(std::uint64_t)) {
        if (size >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::size_t>::max())) {
            return LinuxGuestWorkerArtifactLoadResult::failure(
                error(
                    LinuxGuestWorkerArtifactErrorCode::
                        host_size_unrepresentable,
                    0,
                    size));
        }
    }

    try {
        std::vector<std::byte> bytes(
            static_cast<std::size_t>(
                size));

        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            errno = 0;
            const auto count =
                ::pread(
                    fd,
                    bytes.data() + offset,
                    bytes.size() - offset,
                    static_cast<off_t>(
                        offset));
            if (count == 0) {
                return LinuxGuestWorkerArtifactLoadResult::failure(
                    error(
                        LinuxGuestWorkerArtifactErrorCode::
                            unexpected_eof,
                        0,
                        offset));
            }
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return LinuxGuestWorkerArtifactLoadResult::failure(
                    error(
                        LinuxGuestWorkerArtifactErrorCode::
                            read_failure,
                        errno,
                        offset));
            }
            offset +=
                static_cast<std::size_t>(
                    count);
        }

        return LinuxGuestWorkerArtifactLoadResult::success(
            std::move(bytes));
    } catch (const std::bad_alloc&) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return LinuxGuestWorkerArtifactLoadResult::failure(
            error(
                LinuxGuestWorkerArtifactErrorCode::
                    host_allocation_failure));
    }
#endif
}

}  // namespace astraea::execution
