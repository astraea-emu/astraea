#include <astraea/execution/guest_worker_wire.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using Message =
    astraea::execution::GuestWorkerWireMessage;

template <typename T>
T round_trip(const T& value) {
    const auto encoded =
        astraea::execution::
            encode_guest_worker_wire_message(
                Message{value});
    REQUIRE(encoded.has_value());

    const auto decoded =
        astraea::execution::
            decode_guest_worker_wire_message(
                encoded.value());
    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<T>(decoded.value()));
    return std::get<T>(decoded.value());
}

astraea::execution::GuestWorkerSyscallRequest
syscall_request() {
    return astraea::execution::GuestWorkerSyscallRequest{
        .request_id =
            astraea::execution::GuestRequestId{
                .value = 1U},
        .worker_id =
            astraea::execution::GuestWorkerId{
                .value = 2U},
        .thread_id =
            astraea::execution::GuestThreadId{
                .value = 3U},
        .guest_syscall_number = 4U,
        .arguments =
            std::array<std::uint64_t, 6>{
                5U, 6U, 7U, 8U, 9U, 10U},
        .guest_rip =
            astraea::memory::GuestAddress{11U},
    };
}

}  // namespace

TEST_CASE(
    "worker wire codec round-trips every first-proof message",
    "[execution][c0][wire]") {
    using namespace astraea::execution;

    const GuestWorkerHello hello{};
    REQUIRE(round_trip(hello) == hello);

    const GuestWorkerReady ready{
        .protocol_version = kGuestWorkerProtocolVersion,
        .worker_id = GuestWorkerId{.value = 17U},
    };
    REQUIRE(round_trip(ready) == ready);

    const GuestWorkerRunRequest run{
        .budget_microseconds = 250000U,
    };
    REQUIRE(round_trip(run) == run);

    const auto syscall = syscall_request();
    REQUIRE(round_trip(syscall) == syscall);

    const GuestWorkerSyscallResult result{
        .request_id = syscall.request_id,
        .worker_id = syscall.worker_id,
        .thread_id = syscall.thread_id,
        .return_value = -42,
        .guest_errno = -7,
    };
    REQUIRE(round_trip(result) == result);

    const GuestWorkerStop stop{
        .worker_id = syscall.worker_id,
        .thread_id = syscall.thread_id,
        .reason =
            GuestWorkerStopReason::
                intercepted_syscall,
        .guest_rip = syscall.guest_rip,
    };
    REQUIRE(round_trip(stop) == stop);

    const GuestWorkerFault fault{
        .worker_id = syscall.worker_id,
        .thread_id = syscall.thread_id,
        .kind =
            GuestWorkerFaultKind::
                illegal_instruction,
        .guest_rip = syscall.guest_rip,
        .fault_address =
            astraea::memory::GuestAddress{0xdeadU},
    };
    REQUIRE(round_trip(fault) == fault);

    const GuestWorkerTerminate terminate{
        .reason =
            GuestWorkerTerminationReason::
                unsupported_guest_behavior,
    };
    REQUIRE(round_trip(terminate) == terminate);
}


TEST_CASE(
    "worker wire header inspection returns one exact bounded frame extent",
    "[execution][c0][wire][header]") {
    using namespace astraea::execution;

    const auto encoded =
        encode_guest_worker_wire_message(
            Message{syscall_request()});
    REQUIRE(encoded.has_value());
    REQUIRE(
        encoded->size() ==
        kGuestWorkerWireHeaderSize + 88U);

    const auto header =
        decode_guest_worker_wire_header(
            std::span<const std::byte>{
                encoded->data(),
                kGuestWorkerWireHeaderSize});
    REQUIRE(header.has_value());
    REQUIRE(
        header->kind ==
        GuestWorkerWireMessageKind::syscall_request);
    REQUIRE(header->payload_size == 88U);
    REQUIRE(header->frame_size == encoded->size());

    const auto short_header =
        decode_guest_worker_wire_header(
            std::span<const std::byte>{
                encoded->data(),
                kGuestWorkerWireHeaderSize - 1U});
    REQUIRE_FALSE(short_header.has_value());
    REQUIRE(
        short_header.error().code ==
        GuestWorkerWireErrorCode::frame_too_short);
}

TEST_CASE(
    "HELLO wire fixture is exact little-endian bytes",
    "[execution][c0][wire][fixture]") {
    const auto encoded =
        astraea::execution::
            encode_guest_worker_wire_message(
                Message{
                    astraea::execution::
                        GuestWorkerHello{}});
    REQUIRE(encoded.has_value());

    const std::vector<std::byte> expected{
        std::byte{0x41}, std::byte{0x53},
        std::byte{0x54}, std::byte{0x52},
        std::byte{0x01}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x00},
        std::byte{0x04}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
        std::byte{0x01}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
    };
    REQUIRE(encoded.value() == expected);
}

TEST_CASE(
    "SYSCALL_REQUEST wire fixture preserves exact field order",
    "[execution][c0][wire][fixture][syscall]") {
    const auto encoded =
        astraea::execution::
            encode_guest_worker_wire_message(
                Message{syscall_request()});
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() == 100U);

    const std::vector<std::byte> header{
        std::byte{0x41}, std::byte{0x53},
        std::byte{0x54}, std::byte{0x52},
        std::byte{0x01}, std::byte{0x00},
        std::byte{0x04}, std::byte{0x00},
        std::byte{0x58}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
    };
    REQUIRE(
        std::vector<std::byte>(
            encoded->begin(),
            encoded->begin() + 12) ==
        header);

    for (std::size_t field = 0U;
         field < 11U;
         ++field) {
        const auto offset = 12U + field * 8U;
        REQUIRE(
            (*encoded)[offset] ==
            static_cast<std::byte>(field + 1U));
        for (std::size_t byte = 1U;
             byte < 8U;
             ++byte) {
            REQUIRE(
                (*encoded)[offset + byte] ==
                std::byte{0x00});
        }
    }
}

TEST_CASE(
    "worker wire decoder rejects malformed framing",
    "[execution][c0][wire][negative]") {
    using Error =
        astraea::execution::GuestWorkerWireErrorCode;

    const auto valid =
        astraea::execution::
            encode_guest_worker_wire_message(
                Message{
                    astraea::execution::
                        GuestWorkerHello{}});
    REQUIRE(valid.has_value());

    SECTION("short header") {
        const std::vector<std::byte> short_frame{
            std::byte{0x41}};
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(
                    short_frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::frame_too_short);
    }

    SECTION("magic") {
        auto frame = valid.value();
        frame[0] = std::byte{0x00};
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::invalid_magic);
    }

    SECTION("wire version") {
        auto frame = valid.value();
        frame[4] = std::byte{0x02};
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_wire_version);
    }

    SECTION("kind") {
        auto frame = valid.value();
        frame[6] = std::byte{0xff};
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unknown_message_kind);
    }

    SECTION("oversized declared payload") {
        auto frame = valid.value();
        frame[8] = std::byte{0x81};
        frame[9] = std::byte{0x00};
        frame[10] = std::byte{0x00};
        frame[11] = std::byte{0x00};
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::payload_too_large);
    }

    SECTION("truncated payload") {
        auto frame = valid.value();
        frame.pop_back();
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::payload_size_mismatch);
    }

    SECTION("trailing byte") {
        auto frame = valid.value();
        frame.push_back(std::byte{0x00});
        const auto result =
            astraea::execution::
                decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::payload_size_mismatch);
    }
}

TEST_CASE(
    "worker wire codec rejects invalid typed and decoded values",
    "[execution][c0][wire][negative][value]") {
    using namespace astraea::execution;

    SECTION("zero run budget at encode") {
        const auto result =
            encode_guest_worker_wire_message(
                Message{
                    GuestWorkerRunRequest{
                        .budget_microseconds = 0U,
                    }});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerWireErrorCode::
                invalid_message_value);
    }

    SECTION("zero run budget at decode") {
        const auto valid =
            encode_guest_worker_wire_message(
                Message{
                    GuestWorkerRunRequest{
                        .budget_microseconds = 1U,
                    }});
        REQUIRE(valid.has_value());
        auto frame = valid.value();
        for (std::size_t index = 12U;
             index < 20U;
             ++index) {
            frame[index] = std::byte{0x00};
        }
        const auto result =
            decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerWireErrorCode::
                invalid_message_value);
    }

    SECTION("invalid stop reason at encode") {
        const GuestWorkerStop stop{
            .worker_id = GuestWorkerId{.value = 1U},
            .thread_id = GuestThreadId{.value = 2U},
            .reason =
                static_cast<GuestWorkerStopReason>(99U),
            .guest_rip =
                astraea::memory::GuestAddress{3U},
        };
        const auto result =
            encode_guest_worker_wire_message(
                Message{stop});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerWireErrorCode::
                invalid_message_value);
    }

    SECTION("invalid stop reason at decode") {
        const GuestWorkerStop stop{
            .worker_id = GuestWorkerId{.value = 1U},
            .thread_id = GuestThreadId{.value = 2U},
            .reason =
                GuestWorkerStopReason::
                    intercepted_syscall,
            .guest_rip =
                astraea::memory::GuestAddress{3U},
        };
        const auto valid =
            encode_guest_worker_wire_message(
                Message{stop});
        REQUIRE(valid.has_value());
        auto frame = valid.value();
        frame[28] = std::byte{0x63};
        frame[29] = std::byte{0x00};
        frame[30] = std::byte{0x00};
        frame[31] = std::byte{0x00};

        const auto result =
            decode_guest_worker_wire_message(frame);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerWireErrorCode::
                invalid_message_value);
    }
}
