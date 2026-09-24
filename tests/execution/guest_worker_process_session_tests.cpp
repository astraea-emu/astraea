#include <astraea/execution/guest_worker_process_session.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
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

#if defined(_WIN32)
class TestHandle {
public:
    explicit TestHandle(HANDLE handle) noexcept
        : handle_(handle) {}

    TestHandle(const TestHandle&) = delete;
    TestHandle& operator=(const TestHandle&) = delete;

    ~TestHandle() {
        if (handle_ != nullptr &&
            handle_ != INVALID_HANDLE_VALUE) {
            (void)::CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_ = nullptr;
};
#endif

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
            .syscall_service = {},
            .max_syscall_requests = 0U,
            .resource_policy = std::nullopt,
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

TEST_CASE(
    "supervised session brokers one typed syscall request",
    "[execution][c0][process][syscall]") {
    std::optional<
        astraea::execution::GuestWorkerSyscallRequest>
        observed;

    auto syscall_config =
        config({"--syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [&observed](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            observed = request;
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE(observed.has_value());
    REQUIRE(observed->request_id.value == 41U);
    REQUIRE(observed->worker_id.value == 1U);
    REQUIRE(observed->thread_id.value == 1U);
    REQUIRE(
        observed->guest_syscall_number ==
        0x5152535455565758ULL);
    REQUIRE(
        observed->arguments[0] ==
        0x1111111111111111ULL);
    REQUIRE(
        observed->arguments[1] ==
        0x2222222222222222ULL);
    REQUIRE(
        observed->arguments[2] ==
        0x3333333333333333ULL);
    REQUIRE(
        observed->arguments[3] ==
        0x4444444444444444ULL);
    REQUIRE(
        observed->arguments[4] ==
        0x5555555555555555ULL);
    REQUIRE(
        observed->arguments[5] ==
        0x6666666666666666ULL);
    REQUIRE(
        observed->guest_rip ==
        astraea::memory::GuestAddress{
            0x400100U});
}

TEST_CASE(
    "supervised worker round-trips one registered native syscall and resumes",
    "[execution][c0][process][syscall][native]") {
    std::optional<
        astraea::execution::GuestWorkerSyscallRequest>
        observed;

    auto syscall_config =
        config({"--native-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [&observed](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            observed = request;
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE(observed.has_value());
    REQUIRE(observed->request_id.value == 41U);
    REQUIRE(observed->worker_id.value == 1U);
    REQUIRE(observed->thread_id.value == 1U);
    REQUIRE(
        observed->guest_syscall_number ==
        0x5152535455565758ULL);
    REQUIRE(
        observed->arguments[0] ==
        0x1111111111111111ULL);
    REQUIRE(
        observed->arguments[1] ==
        0x2222222222222222ULL);
    REQUIRE(
        observed->arguments[2] ==
        0x3333333333333333ULL);
    REQUIRE(
        observed->arguments[3] ==
        0x4444444444444444ULL);
    REQUIRE(
        observed->arguments[4] ==
        0x5555555555555555ULL);
    REQUIRE(
        observed->arguments[5] ==
        0x6666666666666666ULL);
    REQUIRE(observed->guest_rip.value() != 0U);
}

TEST_CASE(
    "native worker refuses mismatched controller result before guest re-entry",
    "[execution][c0][process][syscall][native][negative][identity]") {
    auto syscall_config =
        config({"--native-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        astraea::execution::
                            GuestThreadId{
                                .value =
                                    request.thread_id.value +
                                    1U,
                            },
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                protocol_failure);
}

TEST_CASE(
    "worker rejects mismatched syscall result identity before continuing",
    "[execution][c0][process][syscall][negative][identity]") {
    auto syscall_config =
        config({"--syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        astraea::execution::
                            GuestRequestId{
                                .value =
                                    request.request_id.value +
                                    1U,
                            },
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = 99,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                protocol_failure);
}

TEST_CASE(
    "syscall request without configured service tears worker down",
    "[execution][c0][process][syscall][negative][service]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--syscall-roundtrip"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                syscall_service_unavailable);
}

TEST_CASE(
    "controller syscall service may reject a request without resuming worker",
    "[execution][c0][process][syscall][negative][service]") {
    auto syscall_config =
        config({"--syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest&)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return std::nullopt;
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                syscall_service_rejected);
}

TEST_CASE(
    "supervisor propagates native access violation as terminal typed fault",
    "[execution][c0][process][fault][native][access]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--native-access-fault"}));

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_fault.has_value());
    REQUIRE(
        result->terminal_fault->kind ==
        astraea::execution::
            GuestWorkerFaultKind::
                access_violation);
    REQUIRE(result->terminal_fault->worker_id.value == 1U);
    REQUIRE(result->terminal_fault->thread_id.value == 1U);
    REQUIRE(result->terminal_fault->guest_rip.value() != 0U);
    REQUIRE(
        result->terminal_fault->fault_address.value() ==
        0U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::guest_fault);
    REQUIRE(
        result->stop.guest_rip ==
        result->terminal_fault->guest_rip);
    REQUIRE(
        result->stop.thread_id ==
        result->terminal_fault->thread_id);
    REQUIRE(result->syscall_request_count == 0U);
    REQUIRE(result->child_exit_code == 0);

    // Terminal guest faults must not poison process-session reuse.
    const auto subsequent =
        astraea::execution::
            run_guest_worker_process_session(
                config());
    REQUIRE(subsequent.has_value());
    REQUIRE_FALSE(
        subsequent->terminal_fault.has_value());
}

TEST_CASE(
    "supervisor propagates unregistered UD2 as illegal-instruction fault",
    "[execution][c0][process][fault][native][illegal]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({
                    "--native-illegal-instruction-fault"}));

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_fault.has_value());
    REQUIRE(
        result->terminal_fault->kind ==
        astraea::execution::
            GuestWorkerFaultKind::
                illegal_instruction);
    REQUIRE(result->terminal_fault->guest_rip.value() != 0U);
    REQUIRE(
        result->terminal_fault->fault_address.value() ==
        0U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::guest_fault);
    REQUIRE(
        result->stop.guest_rip ==
        result->terminal_fault->guest_rip);
    REQUIRE(result->syscall_request_count == 0U);
    REQUIRE(result->child_exit_code == 0);
}

TEST_CASE(
    "terminal guest fault cannot be followed by resumable syscall event",
    "[execution][c0][process][fault][negative][ordering]") {
    bool service_called = false;
    auto invalid =
        config({"--fault-then-syscall"});
    invalid.max_syscall_requests = 1U;
    invalid.syscall_service =
        [&service_called](
            const astraea::execution::
                GuestWorkerSyscallRequest&)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            service_called = true;
            return std::nullopt;
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                invalid);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                protocol_failure);
    REQUIRE_FALSE(service_called);
}

TEST_CASE(
    "worker runs under explicit kernel resource ceilings",
    "[execution][c0][process][resource-policy]") {
    auto limited = config();

    astraea::execution::GuestWorkerResourcePolicy policy{};
#if !defined(__SANITIZE_ADDRESS__)
    policy.process_memory_limit_bytes =
        16ULL * 1024ULL * 1024ULL * 1024ULL;
#endif
    policy.process_cpu_time_seconds = 30U;
#if defined(__linux__)
    policy.linux_max_open_files = 32U;
    policy.linux_disable_core_dumps = true;
    policy.linux_disable_file_growth = true;
#endif
    limited.resource_policy = policy;

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                limited);

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
}

TEST_CASE(
    "kernel CPU ceiling terminates busy worker before controller deadline",
    "[execution][c0][process][resource-policy][cpu]") {
    auto limited =
        config({"--burn-cpu"}, 5000U);

    astraea::execution::GuestWorkerResourcePolicy policy{};
    policy.process_cpu_time_seconds = 1U;
    limited.resource_policy = policy;

    const auto started =
        std::chrono::steady_clock::now();
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                limited);
    const auto elapsed =
        std::chrono::steady_clock::now() -
        started;

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                unexpected_eof);
    REQUIRE(elapsed < std::chrono::seconds{5});

    const auto subsequent =
        astraea::execution::
            run_guest_worker_process_session(
                config());
    REQUIRE(subsequent.has_value());
    REQUIRE(subsequent->child_exit_code == 0);
}

TEST_CASE(
    "resource policy rejects zero common ceilings",
    "[execution][c0][process][resource-policy][negative]") {
    auto invalid = config();
    astraea::execution::GuestWorkerResourcePolicy policy{};
    policy.process_cpu_time_seconds = 0U;
    invalid.resource_policy = policy;

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

#if defined(_WIN32)

TEST_CASE(
    "Windows worker does not inherit unrelated inheritable parent handle",
    "[execution][c0][process][windows][inheritance]") {
    SECURITY_ATTRIBUTES attributes{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };

    TestHandle unrelated_event{
        ::CreateEventW(
            &attributes,
            TRUE,
            FALSE,
            nullptr)};
    REQUIRE(unrelated_event.get() != nullptr);

    const auto handle_value =
        reinterpret_cast<std::uintptr_t>(
            unrelated_event.get());
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
            unrelated_event.get(),
            0U) ==
        WAIT_TIMEOUT);
}

#endif

#endif
