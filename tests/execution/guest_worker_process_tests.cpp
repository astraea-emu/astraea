#include <astraea/execution/guest_worker_process.hpp>

#include <chrono>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#ifndef ASTRAEA_TEST_GUEST_WORKER_PATH
#error ASTRAEA_TEST_GUEST_WORKER_PATH must name the owned worker executable
#endif

namespace {

std::filesystem::path worker_path() {
    return std::filesystem::path{
        ASTRAEA_TEST_GUEST_WORKER_PATH};
}

}  // namespace

TEST_CASE(
    "separate owned worker process completes handshake run stop and teardown",
    "[execution][c0][worker-process][integration]") {
    using namespace astraea::execution;

    if (!guest_worker_process_proof_available()) {
        SKIP(
            "supervised worker process proof is enabled "
            "on Linux x86-64 and Windows x64");
    }

    const auto result =
        run_guest_worker_process_proof(
            worker_path(),
            100000U,
            10000U);

    REQUIRE(result.has_value());
    REQUIRE(
        result->worker_id ==
        kOwnedGuestWorkerProofId);
    REQUIRE(
        result->stop.worker_id ==
        kOwnedGuestWorkerProofId);
    REQUIRE(
        result->stop.thread_id ==
        kOwnedGuestWorkerProofThreadId);
    REQUIRE(
        result->stop.reason ==
        GuestWorkerStopReason::normal_guest_return);
    REQUIRE(result->stop.guest_rip.value() == 0U);
    REQUIRE(result->worker_exit_code == 0);
    REQUIRE_FALSE(result->forced_termination);
}

TEST_CASE(
    "worker process rejects invalid controller request before launch",
    "[execution][c0][worker-process][negative]") {
    using namespace astraea::execution;

    if (!guest_worker_process_proof_available()) {
        SKIP(
            "supervised worker process proof is enabled "
            "on Linux x86-64 and Windows x64");
    }

    const auto zero_budget =
        run_guest_worker_process_proof(
            worker_path(),
            0U,
            1000U);
    REQUIRE_FALSE(zero_budget.has_value());
    REQUIRE(
        zero_budget.error().code ==
        GuestWorkerProcessErrorCode::
            invalid_run_budget);

    const auto zero_timeout =
        run_guest_worker_process_proof(
            worker_path(),
            1U,
            0U);
    REQUIRE_FALSE(zero_timeout.has_value());
    REQUIRE(
        zero_timeout.error().code ==
        GuestWorkerProcessErrorCode::
            invalid_timeout);
}

TEST_CASE(
    "worker process reports child exit before READY deterministically",
    "[execution][c0][worker-process][negative][exit]") {
    using namespace astraea::execution;

    if (!guest_worker_process_proof_available()) {
        SKIP(
            "supervised worker process proof is enabled "
            "on Linux x86-64 and Windows x64");
    }

    const auto result =
        run_guest_worker_process_proof(
            worker_path(),
            1000U,
            5000U,
            GuestWorkerOwnedFixtureMode::
                exit_before_ready);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        GuestWorkerProcessErrorCode::
            worker_exited_early);
}

TEST_CASE(
    "worker process rejects malformed child wire frame",
    "[execution][c0][worker-process][negative][wire]") {
    using namespace astraea::execution;

    if (!guest_worker_process_proof_available()) {
        SKIP(
            "supervised worker process proof is enabled "
            "on Linux x86-64 and Windows x64");
    }

    const auto result =
        run_guest_worker_process_proof(
            worker_path(),
            1000U,
            5000U,
            GuestWorkerOwnedFixtureMode::
                invalid_frame_before_ready);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        GuestWorkerProcessErrorCode::
            wire_failure);
    REQUIRE(result.error().wire_error.has_value());
    REQUIRE(
        result.error().wire_error->code ==
        GuestWorkerWireErrorCode::
            invalid_magic);
}

TEST_CASE(
    "worker process timeout forcibly cleans stalled child",
    "[execution][c0][worker-process][negative][timeout]") {
    using namespace astraea::execution;

    if (!guest_worker_process_proof_available()) {
        SKIP(
            "supervised worker process proof is enabled "
            "on Linux x86-64 and Windows x64");
    }

    const auto started =
        std::chrono::steady_clock::now();
    const auto result =
        run_guest_worker_process_proof(
            worker_path(),
            1000U,
            100U,
            GuestWorkerOwnedFixtureMode::
                stall_before_ready);
    const auto elapsed =
        std::chrono::steady_clock::now() -
        started;

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        GuestWorkerProcessErrorCode::
            execution_timeout);
    REQUIRE(
        elapsed <
        std::chrono::seconds{5});

    // A second immediate launch succeeding proves the timed-out worker was
    // synchronously torn down rather than leaving the proof wedged.
    const auto recovery =
        run_guest_worker_process_proof(
            worker_path(),
            1000U,
            5000U);
    REQUIRE(recovery.has_value());
}
