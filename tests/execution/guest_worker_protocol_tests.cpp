#include <astraea/execution/guest_worker_protocol.hpp>

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::execution::GuestWorkerSyscallRequest request() {
    return astraea::execution::GuestWorkerSyscallRequest{
        .request_id =
            astraea::execution::GuestRequestId{
                .value = 11U,
            },
        .worker_id =
            astraea::execution::GuestWorkerId{
                .value = 3U,
            },
        .thread_id =
            astraea::execution::GuestThreadId{
                .value = 7U,
            },
        .guest_syscall_number = 0x123U,
        .arguments =
            std::array<std::uint64_t, 6>{
                1U, 2U, 3U, 4U, 5U, 6U},
        .guest_rip =
            astraea::memory::GuestAddress{
                0x400100U},
    };
}

astraea::execution::GuestWorkerSyscallResult
matching_result() {
    return astraea::execution::GuestWorkerSyscallResult{
        .request_id =
            astraea::execution::GuestRequestId{
                .value = 11U,
            },
        .worker_id =
            astraea::execution::GuestWorkerId{
                .value = 3U,
            },
        .thread_id =
            astraea::execution::GuestThreadId{
                .value = 7U,
            },
        .return_value = 42,
        .guest_errno = 0,
    };
}

}  // namespace

TEST_CASE(
    "guest worker handshake accepts only the current protocol version",
    "[execution][guest-worker][protocol]") {
    using namespace astraea::execution;

    const auto valid =
        validate_guest_worker_handshake(
            GuestWorkerHello{},
            GuestWorkerReady{
                .protocol_version =
                    kGuestWorkerProtocolVersion,
                .worker_id =
                    GuestWorkerId{.value = 1U},
            });
    REQUIRE(valid.has_value());

    const auto stale =
        validate_guest_worker_handshake(
            GuestWorkerHello{
                .protocol_version =
                    kGuestWorkerProtocolVersion - 1U,
            },
            GuestWorkerReady{
                .protocol_version =
                    kGuestWorkerProtocolVersion,
                .worker_id =
                    GuestWorkerId{.value = 1U},
            });
    REQUIRE_FALSE(stale.has_value());
    REQUIRE(
        stale.error().code ==
        GuestWorkerProtocolValidationErrorCode::
            unsupported_protocol_version);
}

TEST_CASE(
    "guest worker run request requires a finite nonzero budget",
    "[execution][guest-worker][protocol]") {
    using namespace astraea::execution;

    REQUIRE(
        validate_guest_worker_run_request(
            GuestWorkerRunRequest{
                .budget_microseconds = 1U,
            })
            .has_value());

    const auto zero =
        validate_guest_worker_run_request(
            GuestWorkerRunRequest{
                .budget_microseconds = 0U,
            });
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        GuestWorkerProtocolValidationErrorCode::
            zero_run_budget);
}

TEST_CASE(
    "guest syscall protocol preserves number arguments and guest RIP",
    "[execution][guest-worker][protocol][syscall]") {
    const auto value = request();

    REQUIRE(value.guest_syscall_number == 0x123U);
    REQUIRE(
        value.arguments ==
        std::array<std::uint64_t, 6>{
            1U, 2U, 3U, 4U, 5U, 6U});
    REQUIRE(
        value.guest_rip ==
        astraea::memory::GuestAddress{
            0x400100U});
}

TEST_CASE(
    "guest syscall result must match request worker thread and request identity",
    "[execution][guest-worker][protocol][syscall][negative]") {
    using namespace astraea::execution;

    const auto req = request();

    SECTION("matching") {
        REQUIRE(
            validate_guest_worker_syscall_result(
                req,
                matching_result())
                .has_value());
    }

    SECTION("request") {
        auto result = matching_result();
        result.request_id.value = 12U;
        const auto checked =
            validate_guest_worker_syscall_result(
                req,
                result);
        REQUIRE_FALSE(checked.has_value());
        REQUIRE(
            checked.error().code ==
            GuestWorkerProtocolValidationErrorCode::
                syscall_request_id_mismatch);
    }

    SECTION("worker") {
        auto result = matching_result();
        result.worker_id.value = 4U;
        const auto checked =
            validate_guest_worker_syscall_result(
                req,
                result);
        REQUIRE_FALSE(checked.has_value());
        REQUIRE(
            checked.error().code ==
            GuestWorkerProtocolValidationErrorCode::
                syscall_worker_id_mismatch);
    }

    SECTION("thread") {
        auto result = matching_result();
        result.thread_id.value = 8U;
        const auto checked =
            validate_guest_worker_syscall_result(
                req,
                result);
        REQUIRE_FALSE(checked.has_value());
        REQUIRE(
            checked.error().code ==
            GuestWorkerProtocolValidationErrorCode::
                syscall_thread_id_mismatch);
    }
}

TEST_CASE(
    "guest worker first-proof stop and fault states remain typed",
    "[execution][guest-worker][protocol][stop]") {
    using namespace astraea::execution;

    const GuestWorkerFault fault{
        .worker_id = GuestWorkerId{.value = 1U},
        .thread_id = GuestThreadId{.value = 2U},
        .kind = GuestWorkerFaultKind::illegal_instruction,
        .guest_rip =
            astraea::memory::GuestAddress{0x5000U},
        .fault_address =
            astraea::memory::GuestAddress{0U},
    };
    REQUIRE(
        fault.kind ==
        GuestWorkerFaultKind::illegal_instruction);

    const GuestWorkerStop stop{
        .worker_id = fault.worker_id,
        .thread_id = fault.thread_id,
        .reason =
            GuestWorkerStopReason::
                intercepted_syscall,
        .guest_rip = fault.guest_rip,
    };
    REQUIRE(
        stop.reason ==
        GuestWorkerStopReason::
            intercepted_syscall);
}
