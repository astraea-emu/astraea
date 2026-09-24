#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include <astraea/execution/guest_worker_process_session.hpp>

#include <algorithm>
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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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

#endif

}  // namespace

bool
guest_worker_process_session_available() noexcept {
#if defined(__linux__)
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

#if !defined(__linux__)
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

        auto stop_message =
            receive_message(
                channel.get(),
                deadline);
        if (!stop_message.has_value()) {
            return GuestWorkerProcessSessionRunResult::failure(
                stop_message.error());
        }
        const auto* stop =
            std::get_if<GuestWorkerStop>(
                &stop_message.value());
        if (stop == nullptr) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        unexpected_message));
        }
        if (stop->worker_id != ready->worker_id) {
            return GuestWorkerProcessSessionRunResult::failure(
                error(
                    GuestWorkerProcessSessionErrorCode::
                        worker_identity_mismatch));
        }

        const GuestWorkerTerminate terminate{
            .reason =
                GuestWorkerTerminationReason::
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
                .stop = *stop,
                .child_exit_code =
                    child_exit.value(),
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
