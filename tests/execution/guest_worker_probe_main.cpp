#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/guest_worker_wire.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <csignal>
#include <sys/prctl.h>
#include <sys/types.h>
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
#include <fcntl.h>
#include <io.h>
#endif

namespace {

enum class ProbeMode {
    normal,
    crash_before_ready,
    hang_after_run,
    bad_frame_after_hello,
    syscall_roundtrip,
};

[[nodiscard]] bool configure_binary_stdio() noexcept {
#if defined(_WIN32)
    return
        _setmode(_fileno(stdin), _O_BINARY) != -1 &&
        _setmode(_fileno(stdout), _O_BINARY) != -1;
#else
    return true;
#endif
}

[[nodiscard]] bool bind_lifetime_to_controller() noexcept {
#if defined(__linux__)
    const auto* expected_text =
        std::getenv("ASTRAEA_CONTROLLER_PID");
    if (expected_text == nullptr) {
        return false;
    }

    pid_t expected = 0;
    const auto text =
        std::string_view{expected_text};
    const auto parsed =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            expected);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() ||
        expected <= 1) {
        return false;
    }

    if (::prctl(
            PR_SET_PDEATHSIG,
            SIGKILL) != 0) {
        return false;
    }

    return ::getppid() == expected;
#else
    return true;
#endif
}

[[nodiscard]] bool read_exact(
    std::span<std::byte> bytes) {
    if (bytes.empty()) {
        return true;
    }

    std::cin.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return
        std::cin.good() ||
        (std::cin.eof() &&
         static_cast<std::size_t>(
             std::cin.gcount()) ==
             bytes.size());
}

using ReadResult =
    astraea::core::Result<
        astraea::execution::GuestWorkerWireMessage,
        int>;

[[nodiscard]] ReadResult read_message() {
    using namespace astraea::execution;

    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};
    if (!read_exact(header_bytes)) {
        return ReadResult::failure(1);
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return ReadResult::failure(2);
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
        if (!read_exact(payload)) {
            return ReadResult::failure(3);
        }
    }

    const auto decoded =
        decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) {
        return ReadResult::failure(4);
    }

    return ReadResult::success(
        decoded.value());
}

[[nodiscard]] bool write_message(
    const astraea::execution::
        GuestWorkerWireMessage& message) {
    const auto encoded =
        astraea::execution::
            encode_guest_worker_wire_message(
                message);
    if (!encoded.has_value()) {
        return false;
    }

    std::cout.write(
        reinterpret_cast<const char*>(
            encoded->data()),
        static_cast<std::streamsize>(
            encoded->size()));
    std::cout.flush();
    return std::cout.good();
}

[[nodiscard]] ProbeMode mode_from_args(
    int argc,
    char** argv) noexcept {
    for (int index = 1; index < argc; ++index) {
        const auto argument =
            std::string_view{argv[index]};
        if (argument ==
            "--crash-before-ready") {
            return ProbeMode::crash_before_ready;
        }
        if (argument ==
            "--hang-after-run") {
            return ProbeMode::hang_after_run;
        }
        if (argument ==
            "--bad-frame-after-hello") {
            return ProbeMode::bad_frame_after_hello;
        }
        if (argument ==
            "--syscall-roundtrip") {
            return ProbeMode::syscall_roundtrip;
        }
    }
    return ProbeMode::normal;
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::uintptr_t>
signal_handle_from_args(
    int argc,
    char** argv) noexcept {
    constexpr std::string_view kPrefix =
        "--signal-handle=";

    for (int index = 1; index < argc; ++index) {
        const auto argument =
            std::string_view{argv[index]};
        if (!argument.starts_with(kPrefix)) {
            continue;
        }

        const auto value_text =
            argument.substr(kPrefix.size());
        std::uintptr_t value = 0U;
        const auto parsed =
            std::from_chars(
                value_text.data(),
                value_text.data() +
                    value_text.size(),
                value);
        if (parsed.ec != std::errc{} ||
            parsed.ptr !=
                value_text.data() +
                    value_text.size()) {
            return std::nullopt;
        }
        return value;
    }

    return std::nullopt;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    using namespace astraea::execution;

    if (!configure_binary_stdio()) {
        return 10;
    }
    if (!bind_lifetime_to_controller()) {
        return 11;
    }

#if defined(_WIN32)
    const auto inherited_handle_probe =
        signal_handle_from_args(argc, argv);
    if (inherited_handle_probe.has_value()) {
        (void)::SetEvent(
            reinterpret_cast<HANDLE>(
                inherited_handle_probe.value()));
    }
#endif

    const auto mode =
        mode_from_args(argc, argv);

    const auto hello_message =
        read_message();
    if (!hello_message.has_value()) {
        return 12;
    }
    const auto* hello =
        std::get_if<GuestWorkerHello>(
            &hello_message.value());
    if (hello == nullptr ||
        hello->protocol_version !=
            kGuestWorkerProtocolVersion) {
        return 13;
    }

    if (mode ==
        ProbeMode::crash_before_ready) {
        return 23;
    }

    if (mode ==
        ProbeMode::bad_frame_after_hello) {
        const std::array<std::byte, kGuestWorkerWireHeaderSize>
            invalid_header{};
        std::cout.write(
            reinterpret_cast<const char*>(
                invalid_header.data()),
            static_cast<std::streamsize>(
                invalid_header.size()));
        std::cout.flush();
        return 24;
    }

    constexpr GuestWorkerId kWorkerId{
        .value = 1U,
    };
    constexpr GuestThreadId kThreadId{
        .value = 1U,
    };

    if (!write_message(
            GuestWorkerWireMessage{
                GuestWorkerReady{
                    .protocol_version =
                        kGuestWorkerProtocolVersion,
                    .worker_id = kWorkerId,
                }})) {
        return 14;
    }

    const auto run_message =
        read_message();
    if (!run_message.has_value()) {
        return 15;
    }
    const auto* run =
        std::get_if<GuestWorkerRunRequest>(
            &run_message.value());
    if (run == nullptr ||
        !validate_guest_worker_run_request(
             *run)
             .has_value()) {
        return 16;
    }

    if (mode == ProbeMode::hang_after_run) {
        std::this_thread::sleep_for(
            std::chrono::hours{1});
        return 25;
    }

    GuestWorkerStopReason stop_reason =
        GuestWorkerStopReason::normal_guest_return;
    astraea::memory::GuestAddress stop_rip{0U};

    if (mode == ProbeMode::syscall_roundtrip) {
        const GuestWorkerSyscallRequest syscall_request{
            .request_id =
                GuestRequestId{.value = 41U},
            .worker_id = kWorkerId,
            .thread_id = kThreadId,
            .guest_syscall_number =
                0x5152535455565758ULL,
            .arguments = {
                0x1111111111111111ULL,
                0x2222222222222222ULL,
                0x3333333333333333ULL,
                0x4444444444444444ULL,
                0x5555555555555555ULL,
                0x6666666666666666ULL,
            },
            .guest_rip =
                astraea::memory::GuestAddress{
                    0x400100U},
        };

        if (!write_message(
                GuestWorkerWireMessage{
                    syscall_request})) {
            return 26;
        }

        const auto result_message =
            read_message();
        if (!result_message.has_value()) {
            return 27;
        }

        const auto* syscall_result =
            std::get_if<GuestWorkerSyscallResult>(
                &result_message.value());
        if (syscall_result == nullptr ||
            !validate_guest_worker_syscall_result(
                 syscall_request,
                 *syscall_result)
                 .has_value()) {
            stop_reason =
                GuestWorkerStopReason::
                    protocol_failure;
        }

        stop_rip = syscall_request.guest_rip;
    }

    if (!write_message(
            GuestWorkerWireMessage{
                GuestWorkerStop{
                    .worker_id = kWorkerId,
                    .thread_id = kThreadId,
                    .reason = stop_reason,
                    .guest_rip = stop_rip,
                }})) {
        return 17;
    }

    const auto terminate_message =
        read_message();
    if (!terminate_message.has_value() ||
        std::get_if<GuestWorkerTerminate>(
            &terminate_message.value()) ==
            nullptr) {
        return 18;
    }

    return 0;
}
