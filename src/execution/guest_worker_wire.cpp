#include <astraea/execution/guest_worker_wire.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerWireError error(
    GuestWorkerWireErrorCode code,
    std::size_t byte_offset = 0U,
    std::uint64_t actual_value = 0U) noexcept {
    return GuestWorkerWireError{
        .code = code,
        .byte_offset = byte_offset,
        .actual_value = actual_value,
    };
}

void append_u16(
    std::vector<std::byte>& bytes,
    std::uint16_t value) {
    const auto widened =
        static_cast<std::uint32_t>(value);
    for (unsigned shift = 0U; shift < 16U; shift += 8U) {
        bytes.push_back(
            static_cast<std::byte>(
                (widened >> shift) & 0xffU));
    }
}

void append_u32(
    std::vector<std::byte>& bytes,
    std::uint32_t value) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(
            static_cast<std::byte>(
                (value >> shift) & 0xffU));
    }
}

void append_u64(
    std::vector<std::byte>& bytes,
    std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(
            static_cast<std::byte>(
                (value >> shift) & 0xffU));
    }
}

[[nodiscard]] std::uint16_t read_u16(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    const auto low =
        static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(
                bytes[offset]));
    const auto high =
        static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(
                bytes[offset + 1U]));
    return static_cast<std::uint16_t>(
        low | (high << 8U));
}

[[nodiscard]] std::uint32_t read_u32(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (unsigned index = 0U; index < 4U; ++index) {
        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (unsigned index = 0U; index < 8U; ++index) {
        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

[[nodiscard]] bool valid_fault_kind(
    GuestWorkerFaultKind kind) noexcept {
    switch (header->kind) {
    case GuestWorkerFaultKind::access_violation:
    case GuestWorkerFaultKind::illegal_instruction:
    case GuestWorkerFaultKind::unregistered_trap_site:
    case GuestWorkerFaultKind::protocol_failure:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_stop_reason(
    GuestWorkerStopReason reason) noexcept {
    switch (reason) {
    case GuestWorkerStopReason::normal_guest_return:
    case GuestWorkerStopReason::intercepted_syscall:
    case GuestWorkerStopReason::unsupported_syscall:
    case GuestWorkerStopReason::guest_fault:
    case GuestWorkerStopReason::execution_budget_exhausted:
    case GuestWorkerStopReason::controller_termination:
    case GuestWorkerStopReason::protocol_failure:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_termination_reason(
    GuestWorkerTerminationReason reason) noexcept {
    switch (reason) {
    case GuestWorkerTerminationReason::user_request:
    case GuestWorkerTerminationReason::execution_budget:
    case GuestWorkerTerminationReason::fatal_guest_fault:
    case GuestWorkerTerminationReason::unsupported_guest_behavior:
    case GuestWorkerTerminationReason::protocol_failure:
        return true;
    }
    return false;
}

[[nodiscard]] std::size_t payload_size(
    GuestWorkerWireMessageKind kind) noexcept {
    switch (kind) {
    case GuestWorkerWireMessageKind::hello:
        return 4U;
    case GuestWorkerWireMessageKind::ready:
        return 12U;
    case GuestWorkerWireMessageKind::run_request:
        return 8U;
    case GuestWorkerWireMessageKind::syscall_request:
        return 88U;
    case GuestWorkerWireMessageKind::syscall_result:
        return 36U;
    case GuestWorkerWireMessageKind::stop:
        return 28U;
    case GuestWorkerWireMessageKind::fault:
        return 36U;
    case GuestWorkerWireMessageKind::terminate:
        return 4U;
    }
    return 0U;
}

[[nodiscard]] GuestWorkerWireMessageKind
kind_of(const GuestWorkerWireMessage& message) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, GuestWorkerHello>) {
                return GuestWorkerWireMessageKind::hello;
            } else if constexpr (std::is_same_v<T, GuestWorkerReady>) {
                return GuestWorkerWireMessageKind::ready;
            } else if constexpr (std::is_same_v<T, GuestWorkerRunRequest>) {
                return GuestWorkerWireMessageKind::run_request;
            } else if constexpr (
                std::is_same_v<T, GuestWorkerSyscallRequest>) {
                return GuestWorkerWireMessageKind::syscall_request;
            } else if constexpr (
                std::is_same_v<T, GuestWorkerSyscallResult>) {
                return GuestWorkerWireMessageKind::syscall_result;
            } else if constexpr (std::is_same_v<T, GuestWorkerStop>) {
                return GuestWorkerWireMessageKind::stop;
            } else if constexpr (std::is_same_v<T, GuestWorkerFault>) {
                return GuestWorkerWireMessageKind::fault;
            } else {
                return GuestWorkerWireMessageKind::terminate;
            }
        },
        message);
}

[[nodiscard]] bool valid_message(
    const GuestWorkerWireMessage& message) noexcept {
    return std::visit(
        [](const auto& value) noexcept {
            using T = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<T, GuestWorkerRunRequest>) {
                return value.budget_microseconds != 0U;
            } else if constexpr (
                std::is_same_v<T, GuestWorkerFault>) {
                return valid_fault_kind(value.kind);
            } else if constexpr (
                std::is_same_v<T, GuestWorkerStop>) {
                return valid_stop_reason(value.reason);
            } else if constexpr (
                std::is_same_v<T, GuestWorkerTerminate>) {
                return valid_termination_reason(value.reason);
            } else {
                return true;
            }
        },
        message);
}

void append_payload(
    std::vector<std::byte>& bytes,
    const GuestWorkerWireMessage& message) {
    std::visit(
        [&bytes](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, GuestWorkerHello>) {
                append_u32(bytes, value.protocol_version);
            } else if constexpr (std::is_same_v<T, GuestWorkerReady>) {
                append_u32(bytes, value.protocol_version);
                append_u64(bytes, value.worker_id.value);
            } else if constexpr (
                std::is_same_v<T, GuestWorkerRunRequest>) {
                append_u64(bytes, value.budget_microseconds);
            } else if constexpr (
                std::is_same_v<T, GuestWorkerSyscallRequest>) {
                append_u64(bytes, value.request_id.value);
                append_u64(bytes, value.worker_id.value);
                append_u64(bytes, value.thread_id.value);
                append_u64(bytes, value.guest_syscall_number);
                for (const auto argument : value.arguments) {
                    append_u64(bytes, argument);
                }
                append_u64(bytes, value.guest_rip.value());
            } else if constexpr (
                std::is_same_v<T, GuestWorkerSyscallResult>) {
                append_u64(bytes, value.request_id.value);
                append_u64(bytes, value.worker_id.value);
                append_u64(bytes, value.thread_id.value);
                append_u64(
                    bytes,
                    std::bit_cast<std::uint64_t>(
                        value.return_value));
                append_u32(
                    bytes,
                    std::bit_cast<std::uint32_t>(
                        value.guest_errno));
            } else if constexpr (std::is_same_v<T, GuestWorkerStop>) {
                append_u64(bytes, value.worker_id.value);
                append_u64(bytes, value.thread_id.value);
                append_u32(
                    bytes,
                    static_cast<std::uint32_t>(
                        value.reason));
                append_u64(bytes, value.guest_rip.value());
            } else if constexpr (std::is_same_v<T, GuestWorkerFault>) {
                append_u64(bytes, value.worker_id.value);
                append_u64(bytes, value.thread_id.value);
                append_u32(
                    bytes,
                    static_cast<std::uint32_t>(
                        value.kind));
                append_u64(bytes, value.guest_rip.value());
                append_u64(bytes, value.fault_address.value());
            } else {
                append_u32(
                    bytes,
                    static_cast<std::uint32_t>(
                        value.reason));
            }
        },
        message);
}

[[nodiscard]] bool decode_kind(
    std::uint16_t raw,
    GuestWorkerWireMessageKind& kind) noexcept {
    switch (raw) {
    case 1U:
        kind = GuestWorkerWireMessageKind::hello;
        return true;
    case 2U:
        kind = GuestWorkerWireMessageKind::ready;
        return true;
    case 3U:
        kind = GuestWorkerWireMessageKind::run_request;
        return true;
    case 4U:
        kind = GuestWorkerWireMessageKind::syscall_request;
        return true;
    case 5U:
        kind = GuestWorkerWireMessageKind::syscall_result;
        return true;
    case 6U:
        kind = GuestWorkerWireMessageKind::stop;
        return true;
    case 7U:
        kind = GuestWorkerWireMessageKind::fault;
        return true;
    case 8U:
        kind = GuestWorkerWireMessageKind::terminate;
        return true;
    default:
        return false;
    }
}

}  // namespace

GuestWorkerWireHeaderResult
decode_guest_worker_wire_header(
    std::span<const std::byte> bytes) noexcept {
    if (bytes.size() < kGuestWorkerWireHeaderSize) {
        return GuestWorkerWireHeaderResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    frame_too_short,
                bytes.size()));
    }

    const auto magic = read_u32(bytes, 0U);
    if (magic != kGuestWorkerWireMagic) {
        return GuestWorkerWireHeaderResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    invalid_magic,
                0U,
                magic));
    }

    const auto version = read_u16(bytes, 4U);
    if (version != kGuestWorkerWireVersion) {
        return GuestWorkerWireHeaderResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    unsupported_wire_version,
                4U,
                version));
    }

    GuestWorkerWireMessageKind kind{};
    const auto raw_kind = read_u16(bytes, 6U);
    if (!decode_kind(raw_kind, kind)) {
        return GuestWorkerWireHeaderResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    unknown_message_kind,
                6U,
                raw_kind));
    }

    const auto declared_size =
        static_cast<std::size_t>(
            read_u32(bytes, 8U));
    if (declared_size > kGuestWorkerWireMaxPayloadSize) {
        return GuestWorkerWireHeaderResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    payload_too_large,
                8U,
                declared_size));
    }

    const auto expected_size = payload_size(kind);
    if (declared_size != expected_size) {
        return GuestWorkerWireHeaderResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    payload_size_mismatch,
                8U,
                declared_size));
    }

    return GuestWorkerWireHeaderResult::success(
        GuestWorkerWireHeader{
            .kind = kind,
            .payload_size = declared_size,
            .frame_size =
                kGuestWorkerWireHeaderSize +
                declared_size,
        });
}

GuestWorkerWireEncodeResult
encode_guest_worker_wire_message(
    const GuestWorkerWireMessage& message) {
    if (!valid_message(message)) {
        return GuestWorkerWireEncodeResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    invalid_message_value));
    }

    const auto kind = kind_of(message);
    const auto size = payload_size(kind);
    if (size > kGuestWorkerWireMaxPayloadSize) {
        return GuestWorkerWireEncodeResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    payload_too_large,
                8U,
                size));
    }

    try {
        std::vector<std::byte> bytes;
        bytes.reserve(
            kGuestWorkerWireHeaderSize + size);
        append_u32(bytes, kGuestWorkerWireMagic);
        append_u16(bytes, kGuestWorkerWireVersion);
        append_u16(
            bytes,
            static_cast<std::uint16_t>(kind));
        append_u32(
            bytes,
            static_cast<std::uint32_t>(size));
        append_payload(bytes, message);
        return GuestWorkerWireEncodeResult::success(
            std::move(bytes));
    } catch (const std::bad_alloc&) {
        return GuestWorkerWireEncodeResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GuestWorkerWireEncodeResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    host_allocation_failure));
    }
}

GuestWorkerWireDecodeResult
decode_guest_worker_wire_message(
    std::span<const std::byte> frame) {
    const auto header =
        decode_guest_worker_wire_header(frame);
    if (!header.has_value()) {
        return GuestWorkerWireDecodeResult::failure(
            header.error());
    }

    if (frame.size() != header->frame_size) {
        return GuestWorkerWireDecodeResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    payload_size_mismatch,
                frame.size(),
                frame.size()));
    }

    const std::size_t p = kGuestWorkerWireHeaderSize;
    GuestWorkerWireMessage message =
        GuestWorkerHello{};

    switch (header->kind) {
    case GuestWorkerWireMessageKind::hello:
        message =
            GuestWorkerHello{
                .protocol_version = read_u32(frame, p),
            };
        break;

    case GuestWorkerWireMessageKind::ready:
        message =
            GuestWorkerReady{
                .protocol_version = read_u32(frame, p),
                .worker_id =
                    GuestWorkerId{
                        .value = read_u64(frame, p + 4U),
                    },
            };
        break;

    case GuestWorkerWireMessageKind::run_request:
        message =
            GuestWorkerRunRequest{
                .budget_microseconds =
                    read_u64(frame, p),
            };
        break;

    case GuestWorkerWireMessageKind::syscall_request: {
        GuestWorkerSyscallRequest request{
            .request_id =
                GuestRequestId{
                    .value = read_u64(frame, p),
                },
            .worker_id =
                GuestWorkerId{
                    .value = read_u64(frame, p + 8U),
                },
            .thread_id =
                GuestThreadId{
                    .value = read_u64(frame, p + 16U),
                },
            .guest_syscall_number =
                read_u64(frame, p + 24U),
            .arguments = {},
            .guest_rip =
                astraea::memory::GuestAddress{
                    read_u64(frame, p + 80U)},
        };
        for (std::size_t index = 0U;
             index < request.arguments.size();
             ++index) {
            request.arguments[index] =
                read_u64(
                    frame,
                    p + 32U + index * 8U);
        }
        message = request;
        break;
    }

    case GuestWorkerWireMessageKind::syscall_result:
        message =
            GuestWorkerSyscallResult{
                .request_id =
                    GuestRequestId{
                        .value = read_u64(frame, p),
                    },
                .worker_id =
                    GuestWorkerId{
                        .value = read_u64(frame, p + 8U),
                    },
                .thread_id =
                    GuestThreadId{
                        .value = read_u64(frame, p + 16U),
                    },
                .return_value =
                    std::bit_cast<std::int64_t>(
                        read_u64(frame, p + 24U)),
                .guest_errno =
                    std::bit_cast<std::int32_t>(
                        read_u32(frame, p + 32U)),
            };
        break;

    case GuestWorkerWireMessageKind::stop: {
        const auto raw_reason = read_u32(frame, p + 16U);
        const auto reason =
            static_cast<GuestWorkerStopReason>(
                raw_reason);
        if (!valid_stop_reason(reason)) {
            return GuestWorkerWireDecodeResult::failure(
                error(
                    GuestWorkerWireErrorCode::
                        invalid_message_value,
                    p + 16U,
                    raw_reason));
        }
        message =
            GuestWorkerStop{
                .worker_id =
                    GuestWorkerId{
                        .value = read_u64(frame, p),
                    },
                .thread_id =
                    GuestThreadId{
                        .value = read_u64(frame, p + 8U),
                    },
                .reason = reason,
                .guest_rip =
                    astraea::memory::GuestAddress{
                        read_u64(frame, p + 20U)},
            };
        break;
    }

    case GuestWorkerWireMessageKind::fault: {
        const auto raw_kind_value =
            read_u32(frame, p + 16U);
        const auto fault_kind =
            static_cast<GuestWorkerFaultKind>(
                raw_kind_value);
        if (!valid_fault_kind(fault_kind)) {
            return GuestWorkerWireDecodeResult::failure(
                error(
                    GuestWorkerWireErrorCode::
                        invalid_message_value,
                    p + 16U,
                    raw_kind_value));
        }
        message =
            GuestWorkerFault{
                .worker_id =
                    GuestWorkerId{
                        .value = read_u64(frame, p),
                    },
                .thread_id =
                    GuestThreadId{
                        .value = read_u64(frame, p + 8U),
                    },
                .kind = fault_kind,
                .guest_rip =
                    astraea::memory::GuestAddress{
                        read_u64(frame, p + 20U)},
                .fault_address =
                    astraea::memory::GuestAddress{
                        read_u64(frame, p + 28U)},
            };
        break;
    }

    case GuestWorkerWireMessageKind::terminate: {
        const auto raw_reason = read_u32(frame, p);
        const auto reason =
            static_cast<GuestWorkerTerminationReason>(
                raw_reason);
        if (!valid_termination_reason(reason)) {
            return GuestWorkerWireDecodeResult::failure(
                error(
                    GuestWorkerWireErrorCode::
                        invalid_message_value,
                    p,
                    raw_reason));
        }
        message =
            GuestWorkerTerminate{
                .reason = reason,
            };
        break;
    }
    }

    if (!valid_message(message)) {
        return GuestWorkerWireDecodeResult::failure(
            error(
                GuestWorkerWireErrorCode::
                    invalid_message_value,
                p));
    }

    return GuestWorkerWireDecodeResult::success(
        std::move(message));
}

}  // namespace astraea::execution
