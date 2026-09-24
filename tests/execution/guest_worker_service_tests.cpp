#include <astraea/execution/guest_worker_service.hpp>

#include <variant>

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "owned worker service accepts only HELLO RUN TERMINATE order",
    "[execution][c0][worker-service]") {
    using namespace astraea::execution;

    GuestWorkerServiceState state{};

    const auto hello =
        handle_guest_worker_service_message(
            state,
            GuestWorkerWireMessage{
                GuestWorkerHello{}});
    REQUIRE(hello.has_value());
    REQUIRE(
        state.phase ==
        GuestWorkerServicePhase::ready);
    REQUIRE(hello->response.has_value());
    REQUIRE(
        std::holds_alternative<GuestWorkerReady>(
            hello->response.value()));
    const auto& ready =
        std::get<GuestWorkerReady>(
            hello->response.value());
    REQUIRE(
        ready.protocol_version ==
        kGuestWorkerProtocolVersion);
    REQUIRE(
        ready.worker_id ==
        kOwnedGuestWorkerProofId);

    const auto run =
        handle_guest_worker_service_message(
            state,
            GuestWorkerWireMessage{
                GuestWorkerRunRequest{
                    .budget_microseconds = 1000U,
                }});
    REQUIRE(run.has_value());
    REQUIRE(run->response.has_value());
    REQUIRE(
        std::holds_alternative<GuestWorkerStop>(
            run->response.value()));
    const auto& stop =
        std::get<GuestWorkerStop>(
            run->response.value());
    REQUIRE(
        stop.worker_id ==
        kOwnedGuestWorkerProofId);
    REQUIRE(
        stop.thread_id ==
        kOwnedGuestWorkerProofThreadId);
    REQUIRE(
        stop.reason ==
        GuestWorkerStopReason::normal_guest_return);
    REQUIRE(stop.guest_rip.value() == 0U);

    const auto terminate =
        handle_guest_worker_service_message(
            state,
            GuestWorkerWireMessage{
                GuestWorkerTerminate{}});
    REQUIRE(terminate.has_value());
    REQUIRE(terminate->terminate_process);
    REQUIRE_FALSE(terminate->response.has_value());
    REQUIRE(
        state.phase ==
        GuestWorkerServicePhase::terminated);
}

TEST_CASE(
    "owned worker service rejects invalid sequencing and values",
    "[execution][c0][worker-service][negative]") {
    using namespace astraea::execution;

    SECTION("run before hello") {
        GuestWorkerServiceState state{};
        const auto result =
            handle_guest_worker_service_message(
                state,
                GuestWorkerWireMessage{
                    GuestWorkerRunRequest{
                        .budget_microseconds = 1U,
                    }});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerServiceErrorCode::
                unexpected_message);
    }

    SECTION("stale protocol") {
        GuestWorkerServiceState state{};
        const auto result =
            handle_guest_worker_service_message(
                state,
                GuestWorkerWireMessage{
                    GuestWorkerHello{
                        .protocol_version =
                            kGuestWorkerProtocolVersion +
                            1U,
                    }});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerServiceErrorCode::
                unsupported_protocol_version);
        REQUIRE(
            state.phase ==
            GuestWorkerServicePhase::awaiting_hello);
    }

    SECTION("zero run budget") {
        GuestWorkerServiceState state{
            .phase = GuestWorkerServicePhase::ready,
        };
        const auto result =
            handle_guest_worker_service_message(
                state,
                GuestWorkerWireMessage{
                    GuestWorkerRunRequest{
                        .budget_microseconds = 0U,
                    }});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerServiceErrorCode::
                invalid_run_request);
    }

    SECTION("syscall before guest execution exists") {
        GuestWorkerServiceState state{
            .phase = GuestWorkerServicePhase::ready,
        };
        const auto result =
            handle_guest_worker_service_message(
                state,
                GuestWorkerWireMessage{
                    GuestWorkerSyscallRequest{}});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerServiceErrorCode::
                unexpected_message);
    }

    SECTION("message after terminate") {
        GuestWorkerServiceState state{
            .phase = GuestWorkerServicePhase::ready,
        };
        REQUIRE(
            handle_guest_worker_service_message(
                state,
                GuestWorkerWireMessage{
                    GuestWorkerTerminate{}})
                .has_value());

        const auto result =
            handle_guest_worker_service_message(
                state,
                GuestWorkerWireMessage{
                    GuestWorkerHello{}});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            GuestWorkerServiceErrorCode::
                already_terminated);
    }
}
