#include <astraea/execution/guest_worker_artifact.hpp>

#include <cerrno>
#include <limits>
#include <new>
#include <stdexcept>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace astraea::execution {
namespace {

[[nodiscard]] LinuxWorkerArtifactError make_error(
    LinuxWorkerArtifactErrorCode code,
    std::int64_t platform_error = 0,
    std::uint64_t observed_size = 0U) noexcept {
    return LinuxWorkerArtifactError{
        .code = code,
        .platform_error = platform_error,
        .observed_size = observed_size,
    };
}

#if defined(__linux__)

class ArtifactFdGuard {
public:
    ArtifactFdGuard() = default;

    ArtifactFdGuard(const ArtifactFdGuard&) = delete;
    ArtifactFdGuard& operator=(const ArtifactFdGuard&) = delete;

    ~ArtifactFdGuard() {
        if (open_) {
            (void)::close(kLinuxWorkerArtifactFd);
        }
    }

    [[nodiscard]] int close_now() noexcept {
        if (!open_) {
            return 0;
        }
        open_ = false;
        return ::close(kLinuxWorkerArtifactFd);
    }

private:
    bool open_ = true;
};

#endif

}  // namespace

LinuxWorkerArtifactReadResult
read_linux_sealed_worker_artifact(
    std::size_t max_artifact_bytes) {
    if (max_artifact_bytes == 0U) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    invalid_size_limit));
    }

#if !defined(__linux__)
    return LinuxWorkerArtifactReadResult::failure(
        make_error(
            LinuxWorkerArtifactErrorCode::
                unsupported_platform));
#else
    ArtifactFdGuard fd_guard;

    errno = 0;
    const auto descriptor_flags =
        ::fcntl(
            kLinuxWorkerArtifactFd,
            F_GETFD);
    if (descriptor_flags < 0) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    descriptor_unavailable,
                errno));
    }

    constexpr int kRequiredSeals =
        F_SEAL_WRITE |
        F_SEAL_GROW |
        F_SEAL_SHRINK |
        F_SEAL_SEAL;

    errno = 0;
    const auto seals =
        ::fcntl(
            kLinuxWorkerArtifactFd,
            F_GET_SEALS);
    if (seals < 0) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    seal_query_failure,
                errno));
    }
    if ((seals & kRequiredSeals) !=
        kRequiredSeals) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    missing_required_seals));
    }

    struct stat metadata {};
    errno = 0;
    if (::fstat(
            kLinuxWorkerArtifactFd,
            &metadata) != 0 ||
        !S_ISREG(metadata.st_mode)) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    metadata_failure,
                errno));
    }

    if (metadata.st_size <= 0) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    empty_artifact,
                0,
                metadata.st_size < 0
                    ? 0U
                    : static_cast<std::uint64_t>(
                          metadata.st_size)));
    }

    const auto size =
        static_cast<std::uint64_t>(
            metadata.st_size);
    if (size >
            static_cast<std::uint64_t>(
                max_artifact_bytes) ||
        size >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::size_t>::max())) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    artifact_too_large,
                0,
                size));
    }

    try {
        std::vector<std::byte> bytes(
            static_cast<std::size_t>(size));

        std::size_t offset = 0U;
        while (offset < bytes.size()) {
            const auto count =
                ::pread(
                    kLinuxWorkerArtifactFd,
                    bytes.data() + offset,
                    bytes.size() - offset,
                    static_cast<off_t>(offset));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return LinuxWorkerArtifactReadResult::failure(
                    make_error(
                        LinuxWorkerArtifactErrorCode::
                            read_failure,
                        errno,
                        size));
            }
            if (count == 0) {
                return LinuxWorkerArtifactReadResult::failure(
                    make_error(
                        LinuxWorkerArtifactErrorCode::
                            read_failure,
                        EIO,
                        size));
            }
            offset +=
                static_cast<std::size_t>(count);
        }

        errno = 0;
        if (fd_guard.close_now() != 0) {
            return LinuxWorkerArtifactReadResult::failure(
                make_error(
                    LinuxWorkerArtifactErrorCode::
                        close_failure,
                    errno,
                    size));
        }

        return LinuxWorkerArtifactReadResult::success(
            std::move(bytes));
    } catch (const std::bad_alloc&) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    host_allocation_failure,
                0,
                size));
    } catch (const std::length_error&) {
        return LinuxWorkerArtifactReadResult::failure(
            make_error(
                LinuxWorkerArtifactErrorCode::
                    host_allocation_failure,
                0,
                size));
    }
#endif
}

}  // namespace astraea::execution
