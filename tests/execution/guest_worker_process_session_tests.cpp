#include <astraea/execution/guest_worker_process_session.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

#ifndef ASTRAEA_GUEST_WORKER_PROBE_PATH
#define ASTRAEA_GUEST_WORKER_PROBE_PATH ""
#endif

namespace {

astraea::execution::GuestWorkerProcessSessionConfig
config(
    std::vector<std::string> arguments = {},
    std::uint64_t timeout_milliseconds = 3000U) {
    return astraea::execution::
        GuestWorkerProcessSessionConfig{
            .worker_executable =
                ASTRAEA_GUEST_WORKER_PROBE_PATH,
            .worker_arguments =
                std::move(arguments),
            .run_budget_microseconds =
                100000U,
            .timeout_milliseconds =
                timeout_milliseconds,
        };
}

}  // namespace

TEST_CASE(
    "guest-worker process session availability is platform explicit",
    "[execution][c0][process]") {
#if defined(__linux__) || defined(_WIN32)
    REQUIRE(
        astraea::execution::
            guest_worker_process_session_available());
#else
    REQUIRE_FALSE(
        astraea::execution::
            guest_worker_process_session_available());
#endif
}

TEST_CASE(
    "guest-worker process session rejects invalid configuration before launch",
    "[execution][c0][process][negative]") {
    auto invalid = config();
    invalid.worker_executable = "relative-worker";

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                invalid);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                invalid_config);
}

#if defined(__linux__) || defined(_WIN32)

TEST_CASE(
    "controller and worker complete bounded handshake run stop terminate",
    "[execution][c0][process][supervised]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config());

    REQUIRE(result.has_value());
    REQUIRE(
        result->ready.protocol_version ==
        astraea::execution::
            kGuestWorkerProtocolVersion);
    REQUIRE(result->ready.worker_id.value == 1U);
    REQUIRE(
        result->stop.worker_id ==
        result->ready.worker_id);
    REQUIRE(result->stop.thread_id.value == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE(result->stop.guest_rip.value() == 0U);
    REQUIRE(result->child_exit_code == 0);
}

TEST_CASE(
    "worker crash before READY becomes deterministic EOF",
    "[execution][c0][process][supervised][negative]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config(
                    {"--crash-before-ready"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                unexpected_eof);
}

TEST_CASE(
    "malformed worker frame fails at wire boundary",
    "[execution][c0][process][supervised][negative][wire]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config(
                    {"--bad-frame-after-hello"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                wire_failure);
    REQUIRE(result.error().wire_error.has_value());
    REQUIRE(
        result.error().wire_error->code ==
        astraea::execution::
            GuestWorkerWireErrorCode::
                invalid_magic);
}

TEST_CASE(
    "worker timeout returns only after cleanup and session is reusable",
    "[execution][c0][process][supervised][negative][timeout]") {
    const auto started =
        std::chrono::steady_clock::now();

    const auto timed_out =
        astraea::execution::
            run_guest_worker_process_session(
                config(
                    {"--hang-after-run"},
                    1000U));

    const auto elapsed =
        std::chrono::steady_clock::now() -
        started;

    REQUIRE_FALSE(timed_out.has_value());
    REQUIRE(
        timed_out.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                timeout);
    REQUIRE(
        elapsed <
        std::chrono::seconds{5});

    // A second child must launch and complete immediately after the timed-out
    // child was force-terminated and reaped.
    const auto subsequent =
        astraea::execution::
            run_guest_worker_process_session(
                config());
    REQUIRE(subsequent.has_value());
    REQUIRE(subsequent->child_exit_code == 0);
}

#if defined(_WIN32)

TEST_CASE(
    "Windows worker does not inherit unrelated inheritable parent handle",
    "[execution][c0][process][windows][inheritance]") {
    SECURITY_ATTRIBUTES attributes{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };

    const auto unrelated_event =
        ::CreateEventW(
            &attributes,
            TRUE,
            FALSE,
            nullptr);
    REQUIRE(unrelated_event != nullptr);

    const auto handle_value =
        reinterpret_cast<std::uintptr_t>(
            unrelated_event);
    const auto argument =
        std::string{"--signal-handle="} +
        std::to_string(handle_value);

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({argument}));

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);

    // If broad inheritance leaked this exact event into the worker, the worker
    // would have signaled it before the HELLO/READY exchange.
    REQUIRE(
        ::WaitForSingleObject(
            unrelated_event,
            0U) ==
        WAIT_TIMEOUT);

    REQUIRE(::CloseHandle(unrelated_event));
}

#endif

#endif
