#if defined(__linux__) && defined(__x86_64__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <astraea/execution/guest_worker_process.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__) && defined(__x86_64__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#elif defined(_WIN32) && defined(_M_X64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace astraea::execution {
namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

[[nodiscard]] GuestWorkerProcessError error(
    GuestWorkerProcessErrorCode code,
    std::int64_t native_error = 0,
    std::optional<GuestWorkerWireError> wire_error =
        std::nullopt,
    std::optional<GuestWorkerProtocolValidationError> protocol_error =
        std::nullopt) noexcept {
    return GuestWorkerProcessError{
        .code = code,
        .native_error = native_error,
        .wire_error = wire_error,
        .protocol_error = protocol_error,
    };
}

[[nodiscard]] const char* fixture_argument(
    GuestWorkerOwnedFixtureMode mode) noexcept {
    switch (mode) {
    case GuestWorkerOwnedFixtureMode::normal:
        return nullptr;
    case GuestWorkerOwnedFixtureMode::exit_before_ready:
        return "--exit-before-ready";
    case GuestWorkerOwnedFixtureMode::invalid_frame_before_ready:
        return "--invalid-frame-before-ready";
    case GuestWorkerOwnedFixtureMode::stall_before_ready:
        return "--stall-before-ready";
    }
    return nullptr;
}

#if defined(__linux__) && defined(__x86_64__)

struct NativeWorker {
    pid_t pid = -1;
    int input_fd = -1;
    int output_fd = -1;
    bool active = false;

    NativeWorker() = default;
    NativeWorker(const NativeWorker&) = delete;
    NativeWorker& operator=(const NativeWorker&) = delete;

    ~NativeWorker() {
        if (input_fd >= 0) {
            ::close(input_fd);
        }
        if (output_fd >= 0) {
            ::close(output_fd);
        }
        if (active && pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 &&
                   errno == EINTR) {
            }
        }
    }
};

[[nodiscard]] GuestWorkerProcessError native_error(
    GuestWorkerProcessErrorCode code) noexcept {
    return error(
        code,
        static_cast<std::int64_t>(errno));
}

[[nodiscard]] astraea::core::Result<
    NativeWorker,
    GuestWorkerProcessError>
spawn_worker(
    const std::filesystem::path& worker_executable,
    GuestWorkerOwnedFixtureMode mode) {
    int to_worker[2]{-1, -1};
    int from_worker[2]{-1, -1};

    if (::pipe2(to_worker, O_CLOEXEC) != 0) {
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                native_error(
                    GuestWorkerProcessErrorCode::
                        pipe_creation_failure));
    }
    if (::pipe2(from_worker, O_CLOEXEC) != 0) {
        const auto saved = errno;
        ::close(to_worker[0]);
        ::close(to_worker[1]);
        errno = saved;
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                native_error(
                    GuestWorkerProcessErrorCode::
                        pipe_creation_failure));
    }

    posix_spawn_file_actions_t actions{};
    int spawn_error =
        posix_spawn_file_actions_init(&actions);
    if (spawn_error == 0) {
        spawn_error =
            posix_spawn_file_actions_adddup2(
                &actions,
                to_worker[0],
                STDIN_FILENO);
    }
    if (spawn_error == 0) {
        spawn_error =
            posix_spawn_file_actions_adddup2(
                &actions,
                from_worker[1],
                STDOUT_FILENO);
    }
    if (spawn_error == 0) {
        spawn_error =
            posix_spawn_file_actions_addopen(
                &actions,
                STDERR_FILENO,
                "/dev/null",
                O_WRONLY,
                0);
    }
    if (spawn_error == 0) {
        spawn_error =
            posix_spawn_file_actions_addclosefrom_np(
                &actions,
                3);
    }

    std::string executable;
    try {
        executable = worker_executable.string();
    } catch (const std::bad_alloc&) {
        posix_spawn_file_actions_destroy(&actions);
        ::close(to_worker[0]);
        ::close(to_worker[1]);
        ::close(from_worker[0]);
        ::close(from_worker[1]);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                error(
                    GuestWorkerProcessErrorCode::
                        host_allocation_failure));
    }

    pid_t pid = -1;
    if (spawn_error == 0) {
        std::array<char*, 3> argv{
            executable.data(),
            nullptr,
            nullptr,
        };
        const auto* fixture =
            fixture_argument(mode);
        std::string fixture_storage;
        if (fixture != nullptr) {
            try {
                fixture_storage = fixture;
            } catch (const std::bad_alloc&) {
                spawn_error = ENOMEM;
            }
            if (spawn_error == 0) {
                argv[1] = fixture_storage.data();
            }
        }

        if (spawn_error == 0) {
            spawn_error =
                ::posix_spawn(
                    &pid,
                    executable.c_str(),
                    &actions,
                    nullptr,
                    argv.data(),
                    environ);
        }
    }

    posix_spawn_file_actions_destroy(&actions);
    ::close(to_worker[0]);
    ::close(from_worker[1]);

    if (spawn_error != 0) {
        ::close(to_worker[1]);
        ::close(from_worker[0]);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                error(
                    GuestWorkerProcessErrorCode::
                        spawn_failure,
                    spawn_error));
    }

    return astraea::core::Result<
        NativeWorker,
        GuestWorkerProcessError>::success(
            NativeWorker{
                .pid = pid,
                .input_fd = to_worker[1],
                .output_fd = from_worker[0],
                .active = true,
            });
}

[[nodiscard]] bool write_all(
    NativeWorker& worker,
    std::span<const std::byte> bytes,
    GuestWorkerProcessError& failure) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto count =
            ::write(
                worker.input_fd,
                bytes.data() + offset,
                bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure =
                native_error(
                    GuestWorkerProcessErrorCode::
                        write_failure);
            return false;
        }
        if (count == 0) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        write_failure);
            return false;
        }
        offset +=
            static_cast<std::size_t>(count);
    }
    return true;
}

[[nodiscard]] bool read_exact(
    NativeWorker& worker,
    std::span<std::byte> bytes,
    Deadline deadline,
    GuestWorkerProcessError& failure) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto now = Clock::now();
        if (now >= deadline) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        execution_timeout);
            return false;
        }

        const auto remaining =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    deadline - now);
        const auto timeout =
            static_cast<int>(
                std::max<std::int64_t>(
                    1,
                    std::min<std::int64_t>(
                        remaining.count(),
                        std::numeric_limits<int>::max())));

        pollfd descriptor{
            .fd = worker.output_fd,
            .events =
                static_cast<short>(
                    POLLIN | POLLHUP),
            .revents = 0,
        };
        const auto polled =
            ::poll(
                &descriptor,
                1,
                timeout);
        if (polled < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure =
                native_error(
                    GuestWorkerProcessErrorCode::
                        read_failure);
            return false;
        }
        if (polled == 0) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        execution_timeout);
            return false;
        }

        const auto count =
            ::read(
                worker.output_fd,
                bytes.data() + offset,
                bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure =
                native_error(
                    GuestWorkerProcessErrorCode::
                        read_failure);
            return false;
        }
        if (count == 0) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        worker_exited_early);
            return false;
        }
        offset +=
            static_cast<std::size_t>(count);
    }
    return true;
}

void close_input(NativeWorker& worker) noexcept {
    if (worker.input_fd >= 0) {
        ::close(worker.input_fd);
        worker.input_fd = -1;
    }
}

[[nodiscard]] bool wait_for_exit(
    NativeWorker& worker,
    Deadline deadline,
    std::int32_t& exit_code,
    GuestWorkerProcessError& failure) noexcept {
    for (;;) {
        int status = 0;
        const auto waited =
            ::waitpid(
                worker.pid,
                &status,
                WNOHANG);
        if (waited == worker.pid) {
            worker.active = false;
            if (!WIFEXITED(status)) {
                failure =
                    error(
                        GuestWorkerProcessErrorCode::
                            worker_exit_failure);
                return false;
            }
            exit_code = WEXITSTATUS(status);
            if (exit_code != 0) {
                failure =
                    error(
                        GuestWorkerProcessErrorCode::
                            worker_exit_failure,
                        exit_code);
                return false;
            }
            return true;
        }
        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            failure =
                native_error(
                    GuestWorkerProcessErrorCode::
                        wait_failure);
            return false;
        }
        if (Clock::now() >= deadline) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        execution_timeout);
            return false;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds{1});
    }
}

#elif defined(_WIN32) && defined(_M_X64)

struct NativeWorker {
    HANDLE process = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    bool active = false;

    NativeWorker() = default;
    NativeWorker(const NativeWorker&) = delete;
    NativeWorker& operator=(const NativeWorker&) = delete;

    ~NativeWorker() {
        if (input_write != nullptr) {
            CloseHandle(input_write);
        }
        if (output_read != nullptr) {
            CloseHandle(output_read);
        }
        if (active && process != nullptr) {
            TerminateProcess(process, 0xffU);
            WaitForSingleObject(process, INFINITE);
        }
        if (process != nullptr) {
            CloseHandle(process);
        }
    }
};

[[nodiscard]] GuestWorkerProcessError win_error(
    GuestWorkerProcessErrorCode code) noexcept {
    return error(
        code,
        static_cast<std::int64_t>(
            GetLastError()));
}

[[nodiscard]] astraea::core::Result<
    NativeWorker,
    GuestWorkerProcessError>
spawn_worker(
    const std::filesystem::path& worker_executable,
    GuestWorkerOwnedFixtureMode mode) {
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };

    HANDLE child_input = nullptr;
    HANDLE parent_input = nullptr;
    HANDLE parent_output = nullptr;
    HANDLE child_output = nullptr;

    if (!CreatePipe(
            &child_input,
            &parent_input,
            &security,
            0U) ||
        !SetHandleInformation(
            parent_input,
            HANDLE_FLAG_INHERIT,
            0U) ||
        !CreatePipe(
            &parent_output,
            &child_output,
            &security,
            0U) ||
        !SetHandleInformation(
            parent_output,
            HANDLE_FLAG_INHERIT,
            0U)) {
        const auto failure =
            win_error(
                GuestWorkerProcessErrorCode::
                    pipe_creation_failure);
        if (child_input != nullptr) {
            CloseHandle(child_input);
        }
        if (parent_input != nullptr) {
            CloseHandle(parent_input);
        }
        if (parent_output != nullptr) {
            CloseHandle(parent_output);
        }
        if (child_output != nullptr) {
            CloseHandle(child_output);
        }
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                failure);
    }

    HANDLE null_error =
        CreateFileW(
            L"NUL",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    if (null_error == INVALID_HANDLE_VALUE) {
        const auto failure =
            win_error(
                GuestWorkerProcessErrorCode::
                    spawn_failure);
        CloseHandle(child_input);
        CloseHandle(parent_input);
        CloseHandle(parent_output);
        CloseHandle(child_output);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                failure);
    }

    SIZE_T attribute_size = 0U;
    InitializeProcThreadAttributeList(
        nullptr,
        1U,
        0U,
        &attribute_size);

    std::vector<std::byte> attribute_storage;
    std::wstring application;
    std::vector<wchar_t> command_line;
    try {
        attribute_storage.resize(attribute_size);
        application = worker_executable.wstring();

        std::wstring command =
            L"\"" + application + L"\"";
        const auto* fixture =
            fixture_argument(mode);
        if (fixture != nullptr) {
            command.push_back(L' ');
            while (*fixture != '\0') {
                command.push_back(
                    static_cast<wchar_t>(
                        static_cast<unsigned char>(
                            *fixture)));
                ++fixture;
            }
        }
        command_line.assign(
            command.begin(),
            command.end());
        command_line.push_back(L'\0');
    } catch (const std::bad_alloc&) {
        CloseHandle(child_input);
        CloseHandle(parent_input);
        CloseHandle(parent_output);
        CloseHandle(child_output);
        CloseHandle(null_error);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                error(
                    GuestWorkerProcessErrorCode::
                        host_allocation_failure));
    }

    auto* attributes =
        reinterpret_cast<
            PPROC_THREAD_ATTRIBUTE_LIST>(
                attribute_storage.data());
    if (!InitializeProcThreadAttributeList(
            attributes,
            1U,
            0U,
            &attribute_size)) {
        const auto failure =
            win_error(
                GuestWorkerProcessErrorCode::
                    spawn_failure);
        CloseHandle(child_input);
        CloseHandle(parent_input);
        CloseHandle(parent_output);
        CloseHandle(child_output);
        CloseHandle(null_error);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                failure);
    }

    const std::array<HANDLE, 3> inherited{
        child_input,
        child_output,
        null_error,
    };
    if (!UpdateProcThreadAttribute(
            attributes,
            0U,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(inherited.data()),
            sizeof(inherited),
            nullptr,
            nullptr)) {
        const auto failure =
            win_error(
                GuestWorkerProcessErrorCode::
                    spawn_failure);
        DeleteProcThreadAttributeList(attributes);
        CloseHandle(child_input);
        CloseHandle(parent_input);
        CloseHandle(parent_output);
        CloseHandle(child_output);
        CloseHandle(null_error);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                failure);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_input;
    startup.StartupInfo.hStdOutput = child_output;
    startup.StartupInfo.hStdError = null_error;
    startup.lpAttributeList = attributes;

    PROCESS_INFORMATION process{};
    const BOOL created =
        CreateProcessW(
            application.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            EXTENDED_STARTUPINFO_PRESENT |
                CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup.StartupInfo,
            &process);

    const auto create_error =
        created ? ERROR_SUCCESS : GetLastError();

    DeleteProcThreadAttributeList(attributes);
    CloseHandle(child_input);
    CloseHandle(child_output);
    CloseHandle(null_error);

    if (!created) {
        CloseHandle(parent_input);
        CloseHandle(parent_output);
        return astraea::core::Result<
            NativeWorker,
            GuestWorkerProcessError>::failure(
                error(
                    GuestWorkerProcessErrorCode::
                        spawn_failure,
                    static_cast<std::int64_t>(
                        create_error)));
    }

    CloseHandle(process.hThread);
    return astraea::core::Result<
        NativeWorker,
        GuestWorkerProcessError>::success(
            NativeWorker{
                .process = process.hProcess,
                .input_write = parent_input,
                .output_read = parent_output,
                .active = true,
            });
}

[[nodiscard]] bool write_all(
    NativeWorker& worker,
    std::span<const std::byte> bytes,
    GuestWorkerProcessError& failure) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining =
            bytes.size() - offset;
        const auto request =
            static_cast<DWORD>(
                std::min<std::size_t>(
                    remaining,
                    std::numeric_limits<DWORD>::max()));
        DWORD written = 0U;
        if (!WriteFile(
                worker.input_write,
                bytes.data() + offset,
                request,
                &written,
                nullptr)) {
            failure =
                win_error(
                    GuestWorkerProcessErrorCode::
                        write_failure);
            return false;
        }
        if (written == 0U) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        write_failure);
            return false;
        }
        offset += written;
    }
    return true;
}

[[nodiscard]] bool read_exact(
    NativeWorker& worker,
    std::span<std::byte> bytes,
    Deadline deadline,
    GuestWorkerProcessError& failure) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        DWORD available = 0U;
        if (!PeekNamedPipe(
                worker.output_read,
                nullptr,
                0U,
                nullptr,
                &available,
                nullptr)) {
            const auto native = GetLastError();
            if (native == ERROR_BROKEN_PIPE) {
                failure =
                    error(
                        GuestWorkerProcessErrorCode::
                            worker_exited_early,
                        static_cast<std::int64_t>(
                            native));
            } else {
                failure =
                    error(
                        GuestWorkerProcessErrorCode::
                            read_failure,
                        static_cast<std::int64_t>(
                            native));
            }
            return false;
        }

        if (available != 0U) {
            const auto remaining =
                bytes.size() - offset;
            const auto request =
                static_cast<DWORD>(
                    std::min<std::size_t>(
                        remaining,
                        available));
            DWORD read = 0U;
            if (!ReadFile(
                    worker.output_read,
                    bytes.data() + offset,
                    request,
                    &read,
                    nullptr)) {
                failure =
                    win_error(
                        GuestWorkerProcessErrorCode::
                            read_failure);
                return false;
            }
            if (read == 0U) {
                failure =
                    error(
                        GuestWorkerProcessErrorCode::
                            worker_exited_early);
                return false;
            }
            offset += read;
            continue;
        }

        if (WaitForSingleObject(
                worker.process,
                0U) == WAIT_OBJECT_0) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        worker_exited_early);
            return false;
        }

        if (Clock::now() >= deadline) {
            failure =
                error(
                    GuestWorkerProcessErrorCode::
                        execution_timeout);
            return false;
        }
        Sleep(1U);
    }
    return true;
}

void close_input(NativeWorker& worker) noexcept {
    if (worker.input_write != nullptr) {
        CloseHandle(worker.input_write);
        worker.input_write = nullptr;
    }
}

[[nodiscard]] bool wait_for_exit(
    NativeWorker& worker,
    Deadline deadline,
    std::int32_t& exit_code,
    GuestWorkerProcessError& failure) noexcept {
    const auto now = Clock::now();
    if (now >= deadline) {
        failure =
            error(
                GuestWorkerProcessErrorCode::
                    execution_timeout);
        return false;
    }

    const auto remaining =
        std::chrono::duration_cast<
            std::chrono::milliseconds>(
                deadline - now);
    const auto wait_ms =
        static_cast<DWORD>(
            std::max<std::int64_t>(
                1,
                std::min<std::int64_t>(
                    remaining.count(),
                    INFINITE - 1U)));

    const auto waited =
        WaitForSingleObject(
            worker.process,
            wait_ms);
    if (waited == WAIT_TIMEOUT) {
        failure =
            error(
                GuestWorkerProcessErrorCode::
                    execution_timeout);
        return false;
    }
    if (waited != WAIT_OBJECT_0) {
        failure =
            win_error(
                GuestWorkerProcessErrorCode::
                    wait_failure);
        return false;
    }

    DWORD native_exit = 0U;
    if (!GetExitCodeProcess(
            worker.process,
            &native_exit)) {
        failure =
            win_error(
                GuestWorkerProcessErrorCode::
                    wait_failure);
        return false;
    }

    worker.active = false;
    if (native_exit >
        static_cast<DWORD>(
            std::numeric_limits<std::int32_t>::max())) {
        failure =
            error(
                GuestWorkerProcessErrorCode::
                    worker_exit_failure,
                native_exit);
        return false;
    }

    exit_code =
        static_cast<std::int32_t>(native_exit);
    if (exit_code != 0) {
        failure =
            error(
                GuestWorkerProcessErrorCode::
                    worker_exit_failure,
                exit_code);
        return false;
    }
    return true;
}

#else

struct NativeWorker {};

[[nodiscard]] astraea::core::Result<
    NativeWorker,
    GuestWorkerProcessError>
spawn_worker(
    const std::filesystem::path&,
    GuestWorkerOwnedFixtureMode) {
    return astraea::core::Result<
        NativeWorker,
        GuestWorkerProcessError>::failure(
            error(
                GuestWorkerProcessErrorCode::
                    backend_unavailable));
}

[[nodiscard]] bool write_all(
    NativeWorker&,
    std::span<const std::byte>,
    GuestWorkerProcessError& failure) noexcept {
    failure =
        error(
            GuestWorkerProcessErrorCode::
                backend_unavailable);
    return false;
}

[[nodiscard]] bool read_exact(
    NativeWorker&,
    std::span<std::byte>,
    Deadline,
    GuestWorkerProcessError& failure) noexcept {
    failure =
        error(
            GuestWorkerProcessErrorCode::
                backend_unavailable);
    return false;
}

void close_input(NativeWorker&) noexcept {}

[[nodiscard]] bool wait_for_exit(
    NativeWorker&,
    Deadline,
    std::int32_t&,
    GuestWorkerProcessError& failure) noexcept {
    failure =
        error(
            GuestWorkerProcessErrorCode::
                backend_unavailable);
    return false;
}

#endif

using MessageResult =
    astraea::core::Result<
        GuestWorkerWireMessage,
        GuestWorkerProcessError>;

[[nodiscard]] bool send_message(
    NativeWorker& worker,
    const GuestWorkerWireMessage& message,
    GuestWorkerProcessError& failure) {
    const auto frame =
        encode_guest_worker_wire_message(message);
    if (!frame.has_value()) {
        failure =
            error(
                GuestWorkerProcessErrorCode::
                    wire_failure,
                0,
                frame.error());
        return false;
    }
    return write_all(
        worker,
        frame.value(),
        failure);
}

[[nodiscard]] MessageResult receive_message(
    NativeWorker& worker,
    Deadline deadline) {
    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};
    GuestWorkerProcessError failure{};
    if (!read_exact(
            worker,
            header_bytes,
            deadline,
            failure)) {
        return MessageResult::failure(
            failure);
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return MessageResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    wire_failure,
                0,
                header.error()));
    }

    try {
        std::vector<std::byte> frame(
            header->frame_size);
        std::copy(
            header_bytes.begin(),
            header_bytes.end(),
            frame.begin());

        auto payload =
            std::span<std::byte>{
                frame.data() +
                    kGuestWorkerWireHeaderSize,
                header->payload_size,
            };
        if (!payload.empty() &&
            !read_exact(
                worker,
                payload,
                deadline,
                failure)) {
            return MessageResult::failure(
                failure);
        }

        const auto decoded =
            decode_guest_worker_wire_message(
                frame);
        if (!decoded.has_value()) {
            return MessageResult::failure(
                error(
                    GuestWorkerProcessErrorCode::
                        wire_failure,
                    0,
                    decoded.error()));
        }
        return MessageResult::success(
            decoded.value());
    } catch (const std::bad_alloc&) {
        return MessageResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace

bool
guest_worker_process_proof_available() noexcept {
#if defined(__linux__) && defined(__x86_64__)
    return true;
#elif defined(_WIN32) && defined(_M_X64)
    return true;
#else
    return false;
#endif
}

GuestWorkerProcessProofResult
run_guest_worker_process_proof(
    const std::filesystem::path& worker_executable,
    std::uint64_t run_budget_microseconds,
    std::uint64_t timeout_milliseconds,
    GuestWorkerOwnedFixtureMode fixture_mode) {
    if (!guest_worker_process_proof_available()) {
        return GuestWorkerProcessProofResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    backend_unavailable));
    }

    const GuestWorkerRunRequest run{
        .budget_microseconds =
            run_budget_microseconds,
    };
    const auto valid_run =
        validate_guest_worker_run_request(run);
    if (!valid_run.has_value()) {
        return GuestWorkerProcessProofResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    invalid_run_budget,
                0,
                std::nullopt,
                valid_run.error()));
    }

    if (timeout_milliseconds == 0U ||
        timeout_milliseconds >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return GuestWorkerProcessProofResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    invalid_timeout));
    }

    const auto deadline =
        Clock::now() +
        std::chrono::milliseconds{
            static_cast<std::int64_t>(
                timeout_milliseconds)};

    auto spawned =
        spawn_worker(
            worker_executable,
            fixture_mode);
    if (!spawned.has_value()) {
        return GuestWorkerProcessProofResult::failure(
            spawned.error());
    }
    auto worker = std::move(spawned).value();

    GuestWorkerProcessError failure{};
    const GuestWorkerHello hello{};
    if (!send_message(
            worker,
            GuestWorkerWireMessage{hello},
            failure)) {
        return GuestWorkerProcessProofResult::failure(
            failure);
    }

    auto ready_message =
        receive_message(
            worker,
            deadline);
    if (!ready_message.has_value()) {
        return GuestWorkerProcessProofResult::failure(
            ready_message.error());
    }

    const auto* ready =
        std::get_if<GuestWorkerReady>(
            &ready_message.value());
    if (ready == nullptr) {
        return GuestWorkerProcessProofResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    unexpected_response));
    }

    const auto handshake =
        validate_guest_worker_handshake(
            hello,
            *ready);
    if (!handshake.has_value()) {
        return GuestWorkerProcessProofResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    handshake_failure,
                0,
                std::nullopt,
                handshake.error()));
    }

    if (!send_message(
            worker,
            GuestWorkerWireMessage{run},
            failure)) {
        return GuestWorkerProcessProofResult::failure(
            failure);
    }

    auto stop_message =
        receive_message(
            worker,
            deadline);
    if (!stop_message.has_value()) {
        return GuestWorkerProcessProofResult::failure(
            stop_message.error());
    }

    const auto* stop =
        std::get_if<GuestWorkerStop>(
            &stop_message.value());
    if (stop == nullptr ||
        stop->worker_id != ready->worker_id ||
        stop->thread_id !=
            kOwnedGuestWorkerProofThreadId ||
        stop->reason !=
            GuestWorkerStopReason::
                normal_guest_return ||
        stop->guest_rip.value() != 0U) {
        return GuestWorkerProcessProofResult::failure(
            error(
                GuestWorkerProcessErrorCode::
                    unexpected_response));
    }

    if (!send_message(
            worker,
            GuestWorkerWireMessage{
                GuestWorkerTerminate{
                    .reason =
                        GuestWorkerTerminationReason::
                            user_request,
                }},
            failure)) {
        return GuestWorkerProcessProofResult::failure(
            failure);
    }
    close_input(worker);

    std::int32_t exit_code = 0;
    if (!wait_for_exit(
            worker,
            deadline,
            exit_code,
            failure)) {
        return GuestWorkerProcessProofResult::failure(
            failure);
    }

    return GuestWorkerProcessProofResult::success(
        GuestWorkerProcessProof{
            .worker_id = ready->worker_id,
            .stop = *stop,
            .worker_exit_code = exit_code,
            .forced_termination = false,
        });
}

}  // namespace astraea::execution
