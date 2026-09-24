#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>

namespace astraea::execution {

inline constexpr std::uint32_t kGuestWorkerWireMagic = 0x52545341U;
inline constexpr std::uint16_t kGuestWorkerWireVersion = 1U;
inline constexpr std::size_t kGuestWorkerWireHeaderSize = 12U;
inline constexpr std::size_t kGuestWorkerWireMaxPayloadSize = 128U;

enum class GuestWorkerWireMessageKind : std::uint16_t {
    hello = 1,
    ready = 2,
    run_request = 3,
    syscall_request = 4,
    syscall_result = 5,
    stop = 6,
    fault = 7,
    terminate = 8,
};

using GuestWorkerWireMessage =
    std::variant<
        GuestWorkerHello,
        GuestWorkerReady,
        GuestWorkerRunRequest,
        GuestWorkerSyscallRequest,
        GuestWorkerSyscallResult,
        GuestWorkerStop,
        GuestWorkerFault,
        GuestWorkerTerminate>;

enum class GuestWorkerWireErrorCode {
    frame_too_short,
    invalid_magic,
    unsupported_wire_version,
    unknown_message_kind,
    payload_too_large,
    payload_size_mismatch,
    invalid_message_value,
    host_allocation_failure,
};

struct GuestWorkerWireError {
    GuestWorkerWireErrorCode code =
        GuestWorkerWireErrorCode::frame_too_short;
    std::size_t byte_offset = 0;
    std::uint64_t actual_value = 0;

    auto operator<=>(const GuestWorkerWireError&) const = default;
};

using GuestWorkerWireEncodeResult =
    astraea::core::Result<
        std::vector<std::byte>,
        GuestWorkerWireError>;

using GuestWorkerWireDecodeResult =
    astraea::core::Result<
        GuestWorkerWireMessage,
        GuestWorkerWireError>;

// Encodes one typed protocol message into a bounded, endian-explicit wire
// frame. Native struct layout is never copied into the frame.
[[nodiscard]] GuestWorkerWireEncodeResult
encode_guest_worker_wire_message(
    const GuestWorkerWireMessage& message);

// Decodes exactly one complete frame. Truncation, trailing bytes, unknown
// versions/kinds, oversized payloads, and invalid enum/value encodings fail
// before a trusted typed message is returned.
[[nodiscard]] GuestWorkerWireDecodeResult
decode_guest_worker_wire_message(
    const std::vector<std::byte>& frame);

}  // namespace astraea::execution
