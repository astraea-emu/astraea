#include <astraea/execution/guest_worker_stdio.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <astraea/execution/guest_worker_service.hpp>
#include <astraea/execution/guest_worker_wire.hpp>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerStdioError error(
    GuestWorkerStdioErrorCode code) noexcept {
    return GuestWorkerStdioError{
        .code = code,
    };
}

[[nodiscard]] bool configure_binary_stdio() noexcept {
#if defined(_WIN32)
    return
        _setmode(_fileno(stdin), _O_BINARY) != -1 &&
        _setmode(_fileno(stdout), _O_BINARY) != -1;
#else
    return true;
#endif
}

[[nodiscard]] bool read_exact(
    std::span<std::byte> destination) noexcept {
    std::size_t offset = 0U;
    while (offset < destination.size()) {
        const auto count =
            std::fread(
                destination.data() + offset,
                1U,
                destination.size() - offset,
                stdin);
        if (count == 0U) {
            return false;
        }
        offset += count;
    }
    return true;
}

[[nodiscard]] bool write_exact(
    std::span<const std::byte> source) noexcept {
    std::size_t offset = 0U;
    while (offset < source.size()) {
        const auto count =
            std::fwrite(
                source.data() + offset,
                1U,
                source.size() - offset,
                stdout);
        if (count == 0U) {
            return false;
        }
        offset += count;
    }
    return std::fflush(stdout) == 0;
}

using FrameResult =
    astraea::core::Result<
        std::vector<std::byte>,
        GuestWorkerStdioError>;

[[nodiscard]] FrameResult read_frame() {
    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};
    if (!read_exact(header_bytes)) {
        return FrameResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    input_failure));
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return FrameResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    wire_failure));
    }

    try {
        std::vector<std::byte> frame(
            header->frame_size);
        std::memcpy(
            frame.data(),
            header_bytes.data(),
            header_bytes.size());

        const auto payload =
            std::span<std::byte>{
                frame.data() +
                    kGuestWorkerWireHeaderSize,
                header->payload_size,
            };
        if (!payload.empty() &&
            !read_exact(payload)) {
            return FrameResult::failure(
                error(
                    GuestWorkerStdioErrorCode::
                        input_failure));
        }

        return FrameResult::success(
            std::move(frame));
    } catch (const std::bad_alloc&) {
        return FrameResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace

GuestWorkerStdioResult
run_guest_worker_stdio(
    GuestWorkerOwnedFixtureMode mode) {
    if (!configure_binary_stdio()) {
        return GuestWorkerStdioResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    binary_mode_failure));
    }

    if (mode ==
        GuestWorkerOwnedFixtureMode::
            exit_before_ready) {
        return GuestWorkerStdioResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    service_failure));
    }

    if (mode ==
        GuestWorkerOwnedFixtureMode::
            invalid_frame_before_ready) {
        constexpr std::array<
            std::byte,
            kGuestWorkerWireHeaderSize>
            invalid{};
        if (!write_exact(invalid)) {
            return GuestWorkerStdioResult::failure(
                error(
                    GuestWorkerStdioErrorCode::
                        output_failure));
        }
        return GuestWorkerStdioResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    wire_failure));
    }

    if (mode ==
        GuestWorkerOwnedFixtureMode::
            stall_before_ready) {
        std::this_thread::sleep_for(
            std::chrono::hours{24});
        return GuestWorkerStdioResult::failure(
            error(
                GuestWorkerStdioErrorCode::
                    service_failure));
    }

    GuestWorkerServiceState state{};

    for (;;) {
        auto frame = read_frame();
        if (!frame.has_value()) {
            return GuestWorkerStdioResult::failure(
                frame.error());
        }

        const auto message =
            decode_guest_worker_wire_message(
                frame.value());
        if (!message.has_value()) {
            return GuestWorkerStdioResult::failure(
                error(
                    GuestWorkerStdioErrorCode::
                        wire_failure));
        }

        const auto transition =
            handle_guest_worker_service_message(
                state,
                message.value());
        if (!transition.has_value()) {
            return GuestWorkerStdioResult::failure(
                error(
                    GuestWorkerStdioErrorCode::
                        service_failure));
        }

        if (transition->response.has_value()) {
            const auto encoded =
                encode_guest_worker_wire_message(
                    transition->response.value());
            if (!encoded.has_value()) {
                return GuestWorkerStdioResult::failure(
                    error(
                        GuestWorkerStdioErrorCode::
                            wire_failure));
            }
            if (!write_exact(encoded.value())) {
                return GuestWorkerStdioResult::failure(
                    error(
                        GuestWorkerStdioErrorCode::
                            output_failure));
            }
        }

        if (transition->terminate_process) {
            return GuestWorkerStdioResult::success(
                true);
        }
    }
}

}  // namespace astraea::execution
