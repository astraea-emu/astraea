#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include <astraea/execution/guest_worker_process_session.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerProcessSessionError error(
    GuestWorkerProcessSessionErrorCode code,
    std::int64_t platform_error = 0,
    std::optional<GuestWorkerWireError> wire_error =
        std::nullopt,
    std::optional<GuestWorkerProtocolValidationError>
        protocol_error = std::nullopt) noexcept {
    return GuestWorkerProcessSessionError{
        .code = code,
        .platform_error = platform_error,
        .wire_error = wire_error,
        .protocol_error = protocol_error,
    };
}

[[nodiscard]] bool contains_nul(
    const std::string& value) noexcept {
    return value.find('\0') != std::string::npos;
}

[[nodiscard]] bool config_is_valid(
    const GuestWorkerProcessSessionConfig& config) {
    if (config.worker_executable.empty() ||
        contains_nul(config.worker_executable) ||
        config.run_budget_microseconds == 0U ||
        config.timeout_milliseconds == 0U ||
        config.timeout_milliseconds >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::int64_t>::max())) {
        return false;
    }

    if (!std::filesystem::path{
            config.worker_executable}
             .is_absolute()) {
        return false;
    }

    const bool has_syscall_service =
        static_cast<bool>(
            config.syscall_service);
    const bool accepts_syscalls =
        config.max_syscall_requests != 0U;
    if (has_syscall_service !=
        accepts_syscalls) {
        return false;
    }

    if (config.resource_policy.has_value()) {
        const auto& policy =
            config.resource_policy.value();

        if ((policy.process_memory_limit_bytes.has_value() &&
             policy.process_memory_limit_bytes.value() == 0U) ||
            (policy.process_cpu_time_seconds.has_value() &&
             policy.process_cpu_time_seconds.value() == 0U) ||
            (policy.linux_max_open_files.has_value() &&
             policy.linux_max_open_files.value() < 3U)) {
            return false;
        }

#if !defined(__linux__)
        if (policy.linux_max_open_files.has_value() ||
            policy.linux_disable_core_dumps ||
            policy.linux_disable_file_growth) {
            return false;
        }
#endif
    }

    for (const auto& argument :
         config.worker_arguments) {
        if (contains_nul(argument)) {
            return false;
        }
    }

    return true;
}

#if defined(__linux__)

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept
        : fd_(fd) {}

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~UniqueFd() {
        reset();
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    [[nodiscard]] int release() noexcept {
        return std::exchange(fd_, -1);
    }

    void reset(int replacement = -1) noexcept {
        if (fd_ >= 0) {
            (void)::close(fd_);
        }
        fd_ = replacement;
    }

private:
    int fd_ = -1;
};

class ChildGuard {
public:
    ChildGuard() = default;
    explicit ChildGuard(pid_t pid) noexcept
        : pid_(pid) {}

    ChildGuard(const ChildGuard&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;

    ChildGuard(ChildGuard&& other) noexcept
        : pid_(std::exchange(other.pid_, -1)) {}

    ChildGuard& operator=(ChildGuard&& other) noexcept {
        if (this != &other) {
            terminate_and_reap();
            pid_ = std::exchange(other.pid_, -1);
        }
        return *this;
    }

    ~ChildGuard() {
        terminate_and_reap();
    }

    [[nodiscard]] pid_t pid() const noexcept {
        return pid_;
    }

    void mark_reaped() noexcept {
        pid_ = -1;
    }

    void terminate_and_reap() noexcept {
        if (pid_ <= 0) {
            return;
        }

        if (::kill(pid_, SIGKILL) != 0 &&
            errno != ESRCH) {
            // Destruction still proceeds to waitpid; no exception may escape.
        }

        int status = 0;
        for (;;) {
            const auto waited =
                ::waitpid(pid_, &status, 0);
            if (waited == pid_ ||
                (waited < 0 && errno == ECHILD)) {
                break;
            }
            if (waited < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        pid_ = -1;
    }

private:
    pid_t pid_ = -1;
};

enum class IoStatus {
    ok,
    timeout,
    eof,
    failure,
};

struct IoResult {
    IoStatus status = IoStatus::failure;
    int platform_error = 0;
};

[[nodiscard]] int poll_timeout_ms(
    Deadline deadline) noexcept {
    const auto now = Clock::now();
    if (now >= deadline) {
        return 0;
    }

    const auto remaining =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
            deadline - now);
    if (remaining.count() <= 0) {
        return 1;
    }
    if (remaining.count() >
        static_cast<decltype(remaining.count())>(
            INT_MAX)) {
        return INT_MAX;
    }
    return static_cast<int>(remaining.count());
}

[[nodiscard]] IoResult wait_for_fd(
    int fd,
    short events,
    Deadline deadline) noexcept {
    for (;;) {
        pollfd descriptor{
            .fd = fd,
            .events = events,
            .revents = 0,
        };
        const auto result =
            ::poll(
                &descriptor,
                1,
                poll_timeout_ms(deadline));
        if (result == 0) {
            return IoResult{
                .status = IoStatus::timeout,
                .platform_error = 0,
            };
        }
        if (result < 0) {
            if (errno == EINTR) {
                if (Clock::now() >= deadline) {
                    return IoResult{
                        .status = IoStatus::timeout,
                        .platform_error = 0,
                    };
                }
                continue;
            }
            return IoResult{
                .status = IoStatus::failure,
                .platform_error = errno,
            };
        }

        if ((descriptor.revents & POLLNVAL) != 0) {
            return IoResult{
                .status = IoStatus::failure,
                .platform_error = EBADF,
            };
        }
        if ((descriptor.revents &
             (events | POLLHUP | POLLERR)) != 0) {
            return IoResult{
                .status = IoStatus::ok,
                .platform_error = 0,
            };
        }
    }
}

[[nodiscard]] IoResult read_exact(
    int fd,
    std::span<std::byte> destination,
    Deadline deadline) noexcept {
    std::size_t offset = 0U;
    while (offset < destination.size()) {
        const auto ready =
            wait_for_fd(fd, POLLIN, deadline);
        if (ready.status != IoStatus::ok) {
            return ready;
        }

        const auto count =
            ::recv(
                fd,
                destination.data() + offset,
                destination.size() - offset,
                0);
        if (count == 0) {
            return IoResult{
                .status = IoStatus::eof,
                .platform_error = 0,
            };
        }
        if (count < 0) {
            if (errno == EINTR ||
                errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                continue;
            }
            return IoResult{
                .status = IoStatus::failure,
                .platform_error = errno,
            };
        }
        offset += static_cast<std::size_t>(count);
    }

    return IoResult{
        .status = IoStatus::ok,
        .platform_error = 0,
    };
}

[[nodiscard]] IoResult write_all(
    int fd,
    std::span<const std::byte> source,
    Deadline deadline) noexcept {
    std::size_t offset = 0U;
    while (offset < source.size()) {
        const auto ready =
            wait_for_fd(fd, POLLOUT, deadline);
        if (ready.status != IoStatus::ok) {
            return ready;
        }

        const auto count =
            ::send(
                fd,
                source.data() + offset,
                source.size() - offset,
                MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR ||
                errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                continue;
            }
            return IoResult{
                .status = IoStatus::failure,
                .platform_error = errno,
            };
        }
        if (count == 0) {
            return IoResult{
                .status = IoStatus::failure,
                .platform_error = EPIPE,
            };
        }
        offset += static_cast<std::size_t>(count);
    }

    return IoResult{
        .status = IoStatus::ok,
        .platform_error = 0,
    };
}

[[nodiscard]] astraea::core::Result<
    bool,
    GuestWorkerProcessSessionError>
send_message(
    int fd,
    const GuestWorkerWireMessage& message,
    Deadline deadline) {
    const auto encoded =
        encode_guest_worker_wire_message(message);
    if (!encoded.has_value()) {
        return astraea::core::Result<
            bool,
            GuestWorkerProcessSessionError>::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        wire_failure,
                    0,
                    encoded.error()));
    }

    const auto written =
        write_all(fd, encoded.value(), deadline);
    if (written.status == IoStatus::timeout) {
        return astraea::core::Result<
            bool,
            GuestWorkerProcessSessionError>::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        timeout));
    }
    if (written.status != IoStatus::ok) {
        return astraea::core::Result<
            bool,
            GuestWorkerProcessSessionError>::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        io_failure,
                    written.platform_error));
    }

    return astraea::core::Result<
        bool,
        GuestWorkerProcessSessionError>::success(
            true);
}

using ReceiveMessageResult =
    astraea::core::Result<
        GuestWorkerWireMessage,
        GuestWorkerProcessSessionError>;

[[nodiscard]] ReceiveMessageResult receive_message(
    int fd,
    Deadline deadline) {
    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};
    const auto header_read =
        read_exact(
            fd,
            header_bytes,
            deadline);
    if (header_read.status == IoStatus::timeout) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    timeout));
    }
    if (header_read.status == IoStatus::eof) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    unexpected_eof));
    }
    if (header_read.status != IoStatus::ok) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    io_failure,
                header_read.platform_error));
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    wire_failure,
                0,
                header.error()));
    }

    std::vector<std::byte> frame(
        header->frame_size);
    std::copy(
        header_bytes.begin(),
        header_bytes.end(),
        frame.begin());

    if (header->payload_size != 0U) {
        const auto payload =
            std::span<std::byte>{
                frame.data() +
                    kGuestWorkerWireHeaderSize,
                header->payload_size};
        const auto payload_read =
            read_exact(
                fd,
                payload,
                deadline);
        if (payload_read.status == IoStatus::timeout) {
            return ReceiveMessageResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        timeout));
        }
        if (payload_read.status == IoStatus::eof) {
            return ReceiveMessageResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        unexpected_eof));
        }
        if (payload_read.status != IoStatus::ok) {
            return ReceiveMessageResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        io_failure,
                    payload_read.platform_error));
        }
    }

    const auto decoded =
        decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    wire_failure,
                0,
                decoded.error()));
    }

    return ReceiveMessageResult::success(
        decoded.value());
}

[[nodiscard]] astraea::core::Result<
    std::int32_t,
    GuestWorkerProcessSessionError>
wait_for_child(
    ChildGuard& child,
    Deadline deadline) noexcept {
    for (;;) {
        int status = 0;
        const auto waited =
            ::waitpid(
                child.pid(),
                &status,
                WNOHANG);
        if (waited == child.pid()) {
            child.mark_reaped();

            if (WIFEXITED(status)) {
                return astraea::core::Result<
                    std::int32_t,
                    GuestWorkerProcessSessionError>::
                    success(
                        static_cast<std::int32_t>(
                            WEXITSTATUS(status)));
            }

            const auto signal =
                WIFSIGNALED(status)
                    ? WTERMSIG(status)
                    : 0;
            return astraea::core::Result<
                std::int32_t,
                GuestWorkerProcessSessionError>::
                failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            child_exit_failure,
                        -signal));
        }

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                child.mark_reaped();
            }
            return astraea::core::Result<
                std::int32_t,
                GuestWorkerProcessSessionError>::
                failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            child_exit_failure,
                        errno));
        }

        if (Clock::now() >= deadline) {
            return astraea::core::Result<
                std::int32_t,
                GuestWorkerProcessSessionError>::
                failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            timeout));
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds{1});
    }
}

[[nodiscard]] astraea::core::Result<
    std::pair<UniqueFd, ChildGuard>,
    GuestWorkerProcessSessionError>
spawn_worker(
    const GuestWorkerProcessSessionConfig& config) {
    int sockets[2] = {-1, -1};
    if (::socketpair(
            AF_UNIX,
            SOCK_STREAM | SOCK_CLOEXEC,
            0,
            sockets) != 0) {
        return astraea::core::Result<
            std::pair<UniqueFd, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        channel_creation_failed,
                    errno));
    }

    UniqueFd controller_fd{sockets[0]};
    UniqueFd worker_fd{sockets[1]};

    posix_spawn_file_actions_t actions{};
    const auto init_result =
        ::posix_spawn_file_actions_init(&actions);
    if (init_result != 0) {
        return astraea::core::Result<
            std::pair<UniqueFd, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    init_result));
    }

    const auto destroy_actions =
        [&actions]() noexcept {
            (void)::posix_spawn_file_actions_destroy(
                &actions);
        };

    auto add_result =
        ::posix_spawn_file_actions_adddup2(
            &actions,
            worker_fd.get(),
            STDIN_FILENO);
    if (add_result == 0) {
        add_result =
            ::posix_spawn_file_actions_adddup2(
                &actions,
                worker_fd.get(),
                STDOUT_FILENO);
    }
    if (add_result == 0) {
        add_result =
            ::posix_spawn_file_actions_addopen(
                &actions,
                STDERR_FILENO,
                "/dev/null",
                O_WRONLY,
                0);
    }
    if (add_result == 0) {
        add_result =
            ::posix_spawn_file_actions_addclosefrom_np(
                &actions,
                3);
    }

    if (add_result != 0) {
        destroy_actions();
        return astraea::core::Result<
            std::pair<UniqueFd, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    add_result));
    }

    std::vector<std::string> argument_storage;
    argument_storage.reserve(
        config.worker_arguments.size() + 1U);
    argument_storage.push_back(
        config.worker_executable);
    argument_storage.insert(
        argument_storage.end(),
        config.worker_arguments.begin(),
        config.worker_arguments.end());

    std::vector<char*> argv;
    argv.reserve(
        argument_storage.size() + 1U);
    for (auto& argument : argument_storage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    std::string controller_environment =
        "ASTRAEA_CONTROLLER_PID=" +
        std::to_string(
            static_cast<long long>(::getpid()));
    char* worker_environment[] = {
        controller_environment.data(),
        nullptr,
    };

    pid_t child_pid = -1;
    const auto spawn_result =
        ::posix_spawn(
            &child_pid,
            config.worker_executable.c_str(),
            &actions,
            nullptr,
            argv.data(),
            worker_environment);

    destroy_actions();

    if (spawn_result != 0) {
        return astraea::core::Result<
            std::pair<UniqueFd, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    spawn_result));
    }

    worker_fd.reset();

    return astraea::core::Result<
        std::pair<UniqueFd, ChildGuard>,
        GuestWorkerProcessSessionError>::
        success(
            std::pair<UniqueFd, ChildGuard>{
                std::move(controller_fd),
                ChildGuard{child_pid}});
}

#elif defined(_WIN32)

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

class UniqueHandle {
public:
    UniqueHandle() = default;

    explicit UniqueHandle(HANDLE handle) noexcept
        : handle_(handle) {}

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(
              std::exchange(
                  other.handle_,
                  nullptr)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ =
                std::exchange(
                    other.handle_,
                    nullptr);
        }
        return *this;
    }

    ~UniqueHandle() {
        reset();
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return
            handle_ != nullptr &&
            handle_ != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE release() noexcept {
        return std::exchange(
            handle_,
            nullptr);
    }

    void reset(HANDLE replacement = nullptr) noexcept {
        if (handle_ != nullptr &&
            handle_ != INVALID_HANDLE_VALUE) {
            (void)::CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_ = nullptr;
};

class ChildGuard {
public:
    ChildGuard() = default;

    ChildGuard(
        UniqueHandle process,
        UniqueHandle job) noexcept
        : process_(std::move(process)),
          job_(std::move(job)) {}

    ChildGuard(const ChildGuard&) = delete;
    ChildGuard& operator=(const ChildGuard&) = delete;

    ChildGuard(ChildGuard&&) noexcept = default;
    ChildGuard& operator=(ChildGuard&&) noexcept = default;

    ~ChildGuard() {
        terminate_and_wait();
    }

    [[nodiscard]] HANDLE process() const noexcept {
        return process_.get();
    }

    void mark_exited() noexcept {
        exited_ = true;
    }

    void terminate_and_wait() noexcept {
        if (!process_) {
            return;
        }

        if (!exited_) {
            if (job_) {
                (void)::TerminateJobObject(
                    job_.get(),
                    0xc000013aU);
            } else {
                (void)::TerminateProcess(
                    process_.get(),
                    0xc000013aU);
            }
        }

        (void)::WaitForSingleObject(
            process_.get(),
            INFINITE);
        exited_ = true;
    }

private:
    UniqueHandle process_;
    UniqueHandle job_;
    bool exited_ = false;
};

enum class IoStatus {
    ok,
    timeout,
    eof,
    failure,
};

struct IoResult {
    IoStatus status = IoStatus::failure;
    DWORD platform_error = ERROR_SUCCESS;
};

[[nodiscard]] DWORD wait_timeout_ms(
    Deadline deadline) noexcept {
    const auto now = Clock::now();
    if (now >= deadline) {
        return 0U;
    }

    const auto remaining =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
            deadline - now);

    if (remaining.count() <= 0) {
        return 1U;
    }

    constexpr auto kMaxFiniteWait =
        static_cast<std::int64_t>(
            INFINITE - 1U);
    if (remaining.count() >= kMaxFiniteWait) {
        return INFINITE - 1U;
    }

    return static_cast<DWORD>(
        remaining.count());
}

[[nodiscard]] IoResult complete_overlapped_io(
    HANDLE handle,
    OVERLAPPED& overlapped,
    DWORD& transferred,
    Deadline deadline) noexcept {
    const auto wait_result =
        ::WaitForSingleObject(
            overlapped.hEvent,
            wait_timeout_ms(deadline));

    if (wait_result == WAIT_TIMEOUT) {
        if (!::CancelIoEx(
                handle,
                &overlapped)) {
            const auto cancel_error =
                ::GetLastError();
            if (cancel_error != ERROR_NOT_FOUND) {
                return IoResult{
                    .status = IoStatus::failure,
                    .platform_error = cancel_error,
                };
            }
        }

        (void)::WaitForSingleObject(
            overlapped.hEvent,
            INFINITE);
        return IoResult{
            .status = IoStatus::timeout,
            .platform_error = ERROR_SUCCESS,
        };
    }

    if (wait_result != WAIT_OBJECT_0) {
        return IoResult{
            .status = IoStatus::failure,
            .platform_error =
                wait_result == WAIT_FAILED
                    ? ::GetLastError()
                    : ERROR_GEN_FAILURE,
        };
    }

    if (!::GetOverlappedResult(
            handle,
            &overlapped,
            &transferred,
            FALSE)) {
        const auto io_error = ::GetLastError();
        if (io_error == ERROR_BROKEN_PIPE ||
            io_error == ERROR_NO_DATA) {
            return IoResult{
                .status = IoStatus::eof,
                .platform_error = io_error,
            };
        }
        return IoResult{
            .status = IoStatus::failure,
            .platform_error = io_error,
        };
    }

    return IoResult{
        .status = IoStatus::ok,
        .platform_error = ERROR_SUCCESS,
    };
}

[[nodiscard]] IoResult read_exact(
    HANDLE handle,
    std::span<std::byte> destination,
    Deadline deadline) noexcept {
    std::size_t offset = 0U;

    while (offset < destination.size()) {
        UniqueHandle event{
            ::CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                nullptr)};
        if (!event) {
            return IoResult{
                .status = IoStatus::failure,
                .platform_error =
                    ::GetLastError(),
            };
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = event.get();

        const auto remaining =
            destination.size() - offset;
        const auto request_size =
            static_cast<DWORD>(
                std::min<std::size_t>(
                    remaining,
                    std::numeric_limits<DWORD>::max()));

        DWORD transferred = 0U;
        const auto started =
            ::ReadFile(
                handle,
                destination.data() + offset,
                request_size,
                &transferred,
                &overlapped);

        IoResult result{
            .status = IoStatus::ok,
            .platform_error = ERROR_SUCCESS,
        };

        if (!started) {
            const auto io_error = ::GetLastError();
            if (io_error == ERROR_BROKEN_PIPE ||
                io_error == ERROR_NO_DATA) {
                return IoResult{
                    .status = IoStatus::eof,
                    .platform_error = io_error,
                };
            }
            if (io_error != ERROR_IO_PENDING) {
                return IoResult{
                    .status = IoStatus::failure,
                    .platform_error = io_error,
                };
            }

            result =
                complete_overlapped_io(
                    handle,
                    overlapped,
                    transferred,
                    deadline);
            if (result.status != IoStatus::ok) {
                return result;
            }
        }

        if (transferred == 0U) {
            return IoResult{
                .status = IoStatus::eof,
                .platform_error = ERROR_BROKEN_PIPE,
            };
        }

        offset +=
            static_cast<std::size_t>(
                transferred);
    }

    return IoResult{
        .status = IoStatus::ok,
        .platform_error = ERROR_SUCCESS,
    };
}

[[nodiscard]] IoResult write_all(
    HANDLE handle,
    std::span<const std::byte> source,
    Deadline deadline) noexcept {
    std::size_t offset = 0U;

    while (offset < source.size()) {
        UniqueHandle event{
            ::CreateEventW(
                nullptr,
                TRUE,
                FALSE,
                nullptr)};
        if (!event) {
            return IoResult{
                .status = IoStatus::failure,
                .platform_error =
                    ::GetLastError(),
            };
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = event.get();

        const auto remaining =
            source.size() - offset;
        const auto request_size =
            static_cast<DWORD>(
                std::min<std::size_t>(
                    remaining,
                    std::numeric_limits<DWORD>::max()));

        DWORD transferred = 0U;
        const auto started =
            ::WriteFile(
                handle,
                source.data() + offset,
                request_size,
                &transferred,
                &overlapped);

        IoResult result{
            .status = IoStatus::ok,
            .platform_error = ERROR_SUCCESS,
        };

        if (!started) {
            const auto io_error = ::GetLastError();
            if (io_error == ERROR_BROKEN_PIPE ||
                io_error == ERROR_NO_DATA) {
                return IoResult{
                    .status = IoStatus::eof,
                    .platform_error = io_error,
                };
            }
            if (io_error != ERROR_IO_PENDING) {
                return IoResult{
                    .status = IoStatus::failure,
                    .platform_error = io_error,
                };
            }

            result =
                complete_overlapped_io(
                    handle,
                    overlapped,
                    transferred,
                    deadline);
            if (result.status != IoStatus::ok) {
                return result;
            }
        }

        if (transferred == 0U) {
            return IoResult{
                .status = IoStatus::failure,
                .platform_error = ERROR_WRITE_FAULT,
            };
        }

        offset +=
            static_cast<std::size_t>(
                transferred);
    }

    return IoResult{
        .status = IoStatus::ok,
        .platform_error = ERROR_SUCCESS,
    };
}

[[nodiscard]] astraea::core::Result<
    bool,
    GuestWorkerProcessSessionError>
send_message(
    HANDLE handle,
    const GuestWorkerWireMessage& message,
    Deadline deadline) {
    const auto encoded =
        encode_guest_worker_wire_message(message);
    if (!encoded.has_value()) {
        return astraea::core::Result<
            bool,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        wire_failure,
                    0,
                    encoded.error()));
    }

    const auto written =
        write_all(
            handle,
            encoded.value(),
            deadline);
    if (written.status == IoStatus::timeout) {
        return astraea::core::Result<
            bool,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        timeout));
    }
    if (written.status != IoStatus::ok) {
        return astraea::core::Result<
            bool,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        io_failure,
                    written.platform_error));
    }

    return astraea::core::Result<
        bool,
        GuestWorkerProcessSessionError>::
        success(true);
}

using ReceiveMessageResult =
    astraea::core::Result<
        GuestWorkerWireMessage,
        GuestWorkerProcessSessionError>;

[[nodiscard]] ReceiveMessageResult receive_message(
    HANDLE handle,
    Deadline deadline) {
    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};

    const auto header_read =
        read_exact(
            handle,
            header_bytes,
            deadline);
    if (header_read.status == IoStatus::timeout) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    timeout));
    }
    if (header_read.status == IoStatus::eof) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    unexpected_eof));
    }
    if (header_read.status != IoStatus::ok) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    io_failure,
                header_read.platform_error));
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    wire_failure,
                0,
                header.error()));
    }

    std::vector<std::byte> frame(
        header->frame_size);
    std::copy(
        header_bytes.begin(),
        header_bytes.end(),
        frame.begin());

    if (header->payload_size != 0U) {
        auto payload =
            std::span<std::byte>{
                frame.data() +
                    kGuestWorkerWireHeaderSize,
                header->payload_size};

        const auto payload_read =
            read_exact(
                handle,
                payload,
                deadline);
        if (payload_read.status == IoStatus::timeout) {
            return ReceiveMessageResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        timeout));
        }
        if (payload_read.status == IoStatus::eof) {
            return ReceiveMessageResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        unexpected_eof));
        }
        if (payload_read.status != IoStatus::ok) {
            return ReceiveMessageResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        io_failure,
                    payload_read.platform_error));
        }
    }

    const auto decoded =
        decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) {
        return ReceiveMessageResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    wire_failure,
                0,
                decoded.error()));
    }

    return ReceiveMessageResult::success(
        decoded.value());
}

[[nodiscard]] astraea::core::Result<
    std::int32_t,
    GuestWorkerProcessSessionError>
wait_for_child(
    ChildGuard& child,
    Deadline deadline) noexcept {
    const auto waited =
        ::WaitForSingleObject(
            child.process(),
            wait_timeout_ms(deadline));

    if (waited == WAIT_TIMEOUT) {
        return astraea::core::Result<
            std::int32_t,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        timeout));
    }

    if (waited != WAIT_OBJECT_0) {
        return astraea::core::Result<
            std::int32_t,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        child_exit_failure,
                    waited == WAIT_FAILED
                        ? ::GetLastError()
                        : ERROR_GEN_FAILURE));
    }

    DWORD exit_code = 0U;
    if (!::GetExitCodeProcess(
            child.process(),
            &exit_code)) {
        return astraea::core::Result<
            std::int32_t,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        child_exit_failure,
                    ::GetLastError()));
    }

    child.mark_exited();

    return astraea::core::Result<
        std::int32_t,
        GuestWorkerProcessSessionError>::
        success(
            static_cast<std::int32_t>(
                exit_code));
}

[[nodiscard]] std::optional<std::wstring>
utf8_to_wide(
    const std::string& value) {
    if (value.empty()) {
        return std::wstring{};
    }

    if (value.size() >
        static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    const auto input_size =
        static_cast<int>(value.size());
    const auto required =
        ::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            nullptr,
            0);
    if (required <= 0) {
        return std::nullopt;
    }

    std::wstring result(
        static_cast<std::size_t>(required),
        L'\0');
    if (::MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            input_size,
            result.data(),
            required) != required) {
        return std::nullopt;
    }

    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(
    std::wstring_view argument) {
    const bool requires_quotes =
        argument.empty() ||
        argument.find_first_of(L" \t\"") !=
            std::wstring_view::npos;

    if (!requires_quotes) {
        return std::wstring{argument};
    }

    std::wstring result;
    result.push_back(L'"');

    std::size_t backslashes = 0U;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }

        if (character == L'"') {
            result.append(
                backslashes * 2U + 1U,
                L'\\');
            result.push_back(L'"');
            backslashes = 0U;
            continue;
        }

        result.append(
            backslashes,
            L'\\');
        backslashes = 0U;
        result.push_back(character);
    }

    result.append(
        backslashes * 2U,
        L'\\');
    result.push_back(L'"');
    return result;
}

struct ProcThreadAttributeListGuard {
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;

    ~ProcThreadAttributeListGuard() {
        if (list != nullptr) {
            ::DeleteProcThreadAttributeList(list);
        }
    }
};

[[nodiscard]] astraea::core::Result<
    std::pair<UniqueHandle, ChildGuard>,
    GuestWorkerProcessSessionError>
spawn_worker(
    const GuestWorkerProcessSessionConfig& config) {
    static std::atomic<std::uint64_t> pipe_counter{0U};

    const auto pipe_id =
        pipe_counter.fetch_add(
            1U,
            std::memory_order_relaxed);
    const auto pipe_name =
        std::wstring{LR"(\\.\pipe\astraea-worker-)"} +
        std::to_wstring(::GetCurrentProcessId()) +
        L"-" +
        std::to_wstring(::GetTickCount64()) +
        L"-" +
        std::to_wstring(pipe_id);

    UniqueHandle server{
        ::CreateNamedPipeW(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX |
                FILE_FLAG_OVERLAPPED |
                FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE |
                PIPE_READMODE_BYTE |
                PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            1U,
            4096U,
            4096U,
            0U,
            nullptr)};
    if (!server) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        channel_creation_failed,
                    ::GetLastError()));
    }

    UniqueHandle connect_event{
        ::CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr)};
    if (!connect_event) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        channel_creation_failed,
                    ::GetLastError()));
    }

    OVERLAPPED connect_overlapped{};
    connect_overlapped.hEvent =
        connect_event.get();

    bool connect_pending = false;
    if (!::ConnectNamedPipe(
            server.get(),
            &connect_overlapped)) {
        const auto connect_error =
            ::GetLastError();
        if (connect_error == ERROR_IO_PENDING) {
            connect_pending = true;
        } else if (connect_error !=
                   ERROR_PIPE_CONNECTED) {
            return astraea::core::Result<
                std::pair<UniqueHandle, ChildGuard>,
                GuestWorkerProcessSessionError>::
                failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            channel_creation_failed,
                        connect_error));
        }
    }

    SECURITY_ATTRIBUTES inheritable{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };

    UniqueHandle client{
        ::CreateFileW(
            pipe_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0U,
            &inheritable,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr)};
    if (!client) {
        if (connect_pending) {
            (void)::CancelIoEx(
                server.get(),
                &connect_overlapped);
        }
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        channel_creation_failed,
                    ::GetLastError()));
    }

    if (connect_pending) {
        const auto deadline =
            Clock::now() +
            std::chrono::milliseconds{
                static_cast<std::int64_t>(
                    config.timeout_milliseconds)};
        DWORD transferred = 0U;
        const auto connected =
            complete_overlapped_io(
                server.get(),
                connect_overlapped,
                transferred,
                deadline);
        if (connected.status != IoStatus::ok) {
            return astraea::core::Result<
                std::pair<UniqueHandle, ChildGuard>,
                GuestWorkerProcessSessionError>::
                failure(
                    error(
                        connected.status ==
                                IoStatus::timeout
                            ? GuestWorkerProcessSessionErrorCode::
                                  timeout
                            : GuestWorkerProcessSessionErrorCode::
                                  channel_creation_failed,
                        connected.platform_error));
        }
    }

    UniqueHandle stderr_sink{
        ::CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE,
            &inheritable,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr)};
    if (!stderr_sink) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        channel_creation_failed,
                    ::GetLastError()));
    }

    SIZE_T attribute_bytes = 0U;
    (void)::InitializeProcThreadAttributeList(
        nullptr,
        1U,
        0U,
        &attribute_bytes);
    if (attribute_bytes == 0U) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }

    std::vector<std::byte> attribute_storage(
        attribute_bytes);
    auto* attribute_list =
        reinterpret_cast<
            LPPROC_THREAD_ATTRIBUTE_LIST>(
                attribute_storage.data());
    if (!::InitializeProcThreadAttributeList(
            attribute_list,
            1U,
            0U,
            &attribute_bytes)) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }
    ProcThreadAttributeListGuard attribute_guard{
        .list = attribute_list,
    };

    std::array<HANDLE, 2> inherited_handles{
        client.get(),
        stderr_sink.get(),
    };
    if (!::UpdateProcThreadAttribute(
            attribute_list,
            0U,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited_handles.data(),
            sizeof(inherited_handles),
            nullptr,
            nullptr)) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb =
        sizeof(STARTUPINFOEXW);
    startup.StartupInfo.dwFlags =
        STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput =
        client.get();
    startup.StartupInfo.hStdOutput =
        client.get();
    startup.StartupInfo.hStdError =
        stderr_sink.get();
    startup.lpAttributeList =
        attribute_list;

    const auto executable =
        utf8_to_wide(
            config.worker_executable);
    if (!executable.has_value()) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        invalid_config,
                    ERROR_NO_UNICODE_TRANSLATION));
    }

    std::wstring command_line =
        quote_windows_argument(
            executable.value());
    for (const auto& argument :
         config.worker_arguments) {
        const auto wide_argument =
            utf8_to_wide(argument);
        if (!wide_argument.has_value()) {
            return astraea::core::Result<
                std::pair<UniqueHandle, ChildGuard>,
                GuestWorkerProcessSessionError>::
                failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            invalid_config,
                        ERROR_NO_UNICODE_TRANSLATION));
        }
        command_line.push_back(L' ');
        command_line +=
            quote_windows_argument(
                wide_argument.value());
    }

    std::vector<wchar_t> command_buffer(
        command_line.begin(),
        command_line.end());
    command_buffer.push_back(L'\0');

    std::wstring environment =
        L"ASTRAEA_CONTROLLER_PID=" +
        std::to_wstring(
            ::GetCurrentProcessId());
    environment.push_back(L'\0');
    environment.push_back(L'\0');

    UniqueHandle job{
        ::CreateJobObjectW(
            nullptr,
            nullptr)};
    if (!job) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit =
        1U;
    if (!::SetInformationJobObject(
            job.get(),
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits))) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }

    PROCESS_INFORMATION process_info{};
    if (!::CreateProcessW(
            executable->c_str(),
            command_buffer.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED |
                CREATE_NO_WINDOW |
                CREATE_UNICODE_ENVIRONMENT |
                EXTENDED_STARTUPINFO_PRESENT,
            environment.data(),
            nullptr,
            &startup.StartupInfo,
            &process_info)) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }

    UniqueHandle process{
        process_info.hProcess};
    UniqueHandle thread{
        process_info.hThread};

    if (!::AssignProcessToJobObject(
            job.get(),
            process.get())) {
        const auto assignment_error =
            ::GetLastError();
        (void)::TerminateProcess(
            process.get(),
            0xc000013aU);
        (void)::WaitForSingleObject(
            process.get(),
            INFINITE);
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    assignment_error));
    }

    ChildGuard child{
        std::move(process),
        std::move(job)};

    // The parent copy of the child-facing handles is no longer required.
    client.reset();
    stderr_sink.reset();

    if (::ResumeThread(thread.get()) ==
        static_cast<DWORD>(-1)) {
        return astraea::core::Result<
            std::pair<UniqueHandle, ChildGuard>,
            GuestWorkerProcessSessionError>::
            failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        spawn_failed,
                    ::GetLastError()));
    }

    thread.reset();

    return astraea::core::Result<
        std::pair<UniqueHandle, ChildGuard>,
        GuestWorkerProcessSessionError>::
        success(
            std::pair<UniqueHandle, ChildGuard>{
                std::move(server),
                std::move(child)});
}

#endif

}  // namespace

bool
guest_worker_process_session_available() noexcept {
#if defined(__linux__) || defined(_WIN32)
    return true;
#else
    return false;
#endif
}

GuestWorkerProcessSessionRunResult
run_guest_worker_process_session(
    const GuestWorkerProcessSessionConfig& config) {
    try {
        if (!config_is_valid(config)) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        invalid_config));
        }

        const GuestWorkerRunRequest run_request{
            .budget_microseconds =
                config.run_budget_microseconds,
        };
        const auto validated_run =
            validate_guest_worker_run_request(
                run_request);
        if (!validated_run.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        protocol_failure,
                    0,
                    std::nullopt,
                    validated_run.error()));
        }

#if !defined(__linux__) && !defined(_WIN32)
        return GuestWorkerProcessSessionRunResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    unsupported_platform));
#else
        auto spawned = spawn_worker(config);
        if (!spawned.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                spawned.error());
        }

        auto channel =
            std::move(spawned->first);
        auto child =
            std::move(spawned->second);

        const auto deadline =
            Clock::now() +
            std::chrono::milliseconds{
                static_cast<std::int64_t>(
                    config.timeout_milliseconds)};

        const GuestWorkerHello hello{};
        const auto hello_sent =
            send_message(
                channel.get(),
                GuestWorkerWireMessage{hello},
                deadline);
        if (!hello_sent.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                hello_sent.error());
        }

        auto ready_message =
            receive_message(
                channel.get(),
                deadline);
        if (!ready_message.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                ready_message.error());
        }
        const auto* ready =
            std::get_if<GuestWorkerReady>(
                &ready_message.value());
        if (ready == nullptr) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        unexpected_message));
        }

        const auto handshake =
            validate_guest_worker_handshake(
                hello,
                *ready);
        if (!handshake.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        protocol_failure,
                    0,
                    std::nullopt,
                    handshake.error()));
        }

        const auto run_sent =
            send_message(
                channel.get(),
                GuestWorkerWireMessage{run_request},
                deadline);
        if (!run_sent.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                run_sent.error());
        }

        std::optional<GuestWorkerStop> stop;
        std::optional<GuestWorkerFault> terminal_fault;
        std::size_t syscall_request_count = 0U;

        while (!stop.has_value()) {
            auto worker_message =
                receive_message(
                    channel.get(),
                    deadline);
            if (!worker_message.has_value()) {
                return GuestWorkerProcessSessionRunResult::failure(
                    worker_message.error());
            }

            if (const auto* worker_stop =
                    std::get_if<GuestWorkerStop>(
                        &worker_message.value());
                worker_stop != nullptr) {
                if (worker_stop->worker_id !=
                    ready->worker_id) {
                    return GuestWorkerProcessSessionRunResult::failure(
                        error(
                            GuestWorkerProcessSessionErrorCode::
                                worker_identity_mismatch));
                }

                if (terminal_fault.has_value()) {
                    if (worker_stop->reason !=
                            GuestWorkerStopReason::guest_fault ||
                        worker_stop->thread_id !=
                            terminal_fault->thread_id ||
                        worker_stop->guest_rip !=
                            terminal_fault->guest_rip) {
                        return GuestWorkerProcessSessionRunResult::failure(
                            error(
                                GuestWorkerProcessSessionErrorCode::
                                    protocol_failure));
                    }
                } else if (
                    worker_stop->reason ==
                    GuestWorkerStopReason::guest_fault) {
                    return GuestWorkerProcessSessionRunResult::failure(
                        error(
                            GuestWorkerProcessSessionErrorCode::
                                protocol_failure));
                }

                stop = *worker_stop;
                continue;
            }

            if (const auto* worker_fault =
                    std::get_if<GuestWorkerFault>(
                        &worker_message.value());
                worker_fault != nullptr) {
                if (terminal_fault.has_value() ||
                    worker_fault->worker_id !=
                        ready->worker_id) {
                    return GuestWorkerProcessSessionRunResult::failure(
                        error(
                            terminal_fault.has_value()
                                ? GuestWorkerProcessSessionErrorCode::
                                      protocol_failure
                                : GuestWorkerProcessSessionErrorCode::
                                      worker_identity_mismatch));
                }
                if (worker_fault->thread_id.value == 0U) {
                    return GuestWorkerProcessSessionRunResult::failure(
                        error(
                            GuestWorkerProcessSessionErrorCode::
                                protocol_failure));
                }
                terminal_fault = *worker_fault;
                continue;
            }

            if (terminal_fault.has_value()) {
                return GuestWorkerProcessSessionRunResult::failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            protocol_failure));
            }

            const auto* syscall_request =
                std::get_if<GuestWorkerSyscallRequest>(
                    &worker_message.value());
            if (syscall_request == nullptr) {
                return GuestWorkerProcessSessionRunResult::failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            unexpected_message));
            }

            if (syscall_request->worker_id !=
                ready->worker_id) {
                return GuestWorkerProcessSessionRunResult::failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            worker_identity_mismatch));
            }

            if (!config.syscall_service) {
                return GuestWorkerProcessSessionRunResult::failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            syscall_service_unavailable));
            }

            if (syscall_request_count >=
                config.max_syscall_requests) {
                return GuestWorkerProcessSessionRunResult::failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            syscall_request_limit_exceeded));
            }

            const auto syscall_result =
                config.syscall_service(
                    *syscall_request);
            if (!syscall_result.has_value()) {
                return GuestWorkerProcessSessionRunResult::failure(
                    error(
                        GuestWorkerProcessSessionErrorCode::
                            syscall_service_rejected));
            }

            const auto result_sent =
                send_message(
                    channel.get(),
                    GuestWorkerWireMessage{
                        syscall_result.value()},
                    deadline);
            if (!result_sent.has_value()) {
                return GuestWorkerProcessSessionRunResult::failure(
                    result_sent.error());
            }

            ++syscall_request_count;
        }

        const GuestWorkerTerminate terminate{
            .reason =
                terminal_fault.has_value()
                    ? GuestWorkerTerminationReason::
                          fatal_guest_fault
                    : GuestWorkerTerminationReason::
                          user_request,
        };
        const auto terminate_sent =
            send_message(
                channel.get(),
                GuestWorkerWireMessage{terminate},
                deadline);
        if (!terminate_sent.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                terminate_sent.error());
        }

        const auto child_exit =
            wait_for_child(
                child,
                deadline);
        if (!child_exit.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                child_exit.error());
        }
        if (child_exit.value() != 0) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        child_exit_failure,
                    child_exit.value()));
        }

        return GuestWorkerProcessSessionRunResult::success(
            GuestWorkerProcessSessionResult{
                .ready = *ready,
                .stop = stop.value(),
                .child_exit_code =
                    child_exit.value(),
                .syscall_request_count =
                    syscall_request_count,
                .terminal_fault =
                    terminal_fault,
            });
#endif
    } catch (const std::bad_alloc&) {
        return GuestWorkerProcessSessionRunResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GuestWorkerProcessSessionRunResult::failure(
            error(
                GuestWorkerProcessSessionErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::execution
