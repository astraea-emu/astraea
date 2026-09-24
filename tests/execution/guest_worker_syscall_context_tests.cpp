#include <astraea/execution/guest_worker_syscall_context.hpp>

#include <bit>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::execution::ExecutionStop registered_stop(
    std::uint64_t rip = 0x400100U) {
    astraea::execution::GuestCpuContext context{
        .rax = 0x101U,
        .rbx = 0x202U,
        .rcx = 0x303U,
        .rdx = 0x404U,
        .rsi = 0x505U,
        .rdi = 0x606U,
        .rbp = 0x707U,
        .rsp = 0x808U,
        .r8 = 0x909U,
        .r9 = 0xa0aU,
        .r10 = 0xb0bU,
        .r11 = 0xc0cU,
        .r12 = 0xd0dU,
        .r13 = 0xe0eU,
        .r14 = 0xf0fU,
        .r15 = 0x1110U,
        .rip = rip,
        .rflags = 0x202U,
        .fs_base = 0x12340000U,
        .gs_base = 0x56780000U,
    };

    return astraea::execution::ExecutionStop{
        .reason =
            astraea::execution::
                ExecutionStopReason::
                    registered_syscall_trap,
        .context = context,
        .has_gate_slot = false,
        .gate_slot = 0U,
        .has_fault = false,
        .fault = {},
    };
}

astraea::execution::RegisteredSyscallTrapSite trap(
    std::uint64_t rip = 0x400100U) {
    return astraea::execution::
        RegisteredSyscallTrapSite{
            .guest_rip =
                astraea::memory::GuestAddress{rip},
            .code_byte_offset = 17U,
            .original_bytes =
                astraea::execution::
                    kX86SyscallBytes,
            .trap_bytes =
                astraea::execution::
                    kX86Ud2Bytes,
        };
}

astraea::execution::GuestWorkerSyscallRequest request() {
    const auto projected =
        astraea::execution::
            project_registered_syscall_request(
                registered_stop(),
                astraea::execution::
                    GuestRequestId{.value = 11U},
                astraea::execution::
                    GuestWorkerId{.value = 3U},
                astraea::execution::
                    GuestThreadId{.value = 7U});
    REQUIRE(projected.has_value());
    return projected.value();
}

astraea::execution::GuestWorkerSyscallResult result() {
    return astraea::execution::
        GuestWorkerSyscallResult{
            .request_id =
                astraea::execution::
                    GuestRequestId{.value = 11U},
            .worker_id =
                astraea::execution::
                    GuestWorkerId{.value = 3U},
            .thread_id =
                astraea::execution::
                    GuestThreadId{.value = 7U},
            .return_value = -0x12345,
            .guest_errno = 0,
        };
}

}  // namespace

TEST_CASE(
    "registered syscall stop projects the bounded synthetic x86-64 register contract",
    "[execution][c0][syscall-context][projection]") {
    const auto stop = registered_stop();

    const auto projected =
        astraea::execution::
            project_registered_syscall_request(
                stop,
                astraea::execution::
                    GuestRequestId{.value = 11U},
                astraea::execution::
                    GuestWorkerId{.value = 3U},
                astraea::execution::
                    GuestThreadId{.value = 7U});

    REQUIRE(projected.has_value());
    REQUIRE(projected->request_id.value == 11U);
    REQUIRE(projected->worker_id.value == 3U);
    REQUIRE(projected->thread_id.value == 7U);
    REQUIRE(
        projected->guest_syscall_number ==
        stop.context.rax);
    REQUIRE(projected->arguments[0] == stop.context.rdi);
    REQUIRE(projected->arguments[1] == stop.context.rsi);
    REQUIRE(projected->arguments[2] == stop.context.rdx);
    REQUIRE(projected->arguments[3] == stop.context.r10);
    REQUIRE(projected->arguments[4] == stop.context.r8);
    REQUIRE(projected->arguments[5] == stop.context.r9);
    REQUIRE(
        projected->guest_rip ==
        astraea::memory::GuestAddress{
            stop.context.rip});
}

TEST_CASE(
    "registered syscall result changes only RAX and RIP by two bytes",
    "[execution][c0][syscall-context][resume]") {
    const auto stop = registered_stop();
    const auto req = request();
    const auto res = result();

    const auto resumed =
        astraea::execution::
            apply_registered_syscall_result_to_context(
                stop,
                trap(),
                req,
                res);

    REQUIRE(resumed.has_value());

    auto expected = stop.context;
    expected.rax =
        std::bit_cast<std::uint64_t>(
            res.return_value);
    expected.rip += 2U;

    REQUIRE(resumed.value() == expected);
}

TEST_CASE(
    "syscall context projection rejects invalid stop shape and zero identities",
    "[execution][c0][syscall-context][negative][projection]") {
    using Error =
        astraea::execution::
            GuestWorkerSyscallContextErrorCode;

    SECTION("wrong stop reason") {
        auto stop = registered_stop();
        stop.reason =
            astraea::execution::
                ExecutionStopReason::guest_fault;

        const auto projected =
            astraea::execution::
                project_registered_syscall_request(
                    stop,
                    astraea::execution::
                        GuestRequestId{.value = 11U},
                    astraea::execution::
                        GuestWorkerId{.value = 3U},
                    astraea::execution::
                        GuestThreadId{.value = 7U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Error::invalid_stop_reason);
    }

    SECTION("gate-bearing trap stop") {
        auto stop = registered_stop();
        stop.has_gate_slot = true;

        const auto projected =
            astraea::execution::
                project_registered_syscall_request(
                    stop,
                    astraea::execution::
                        GuestRequestId{.value = 11U},
                    astraea::execution::
                        GuestWorkerId{.value = 3U},
                    astraea::execution::
                        GuestThreadId{.value = 7U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Error::stop_has_gate);
    }

    SECTION("fault-bearing trap stop") {
        auto stop = registered_stop();
        stop.has_fault = true;

        const auto projected =
            astraea::execution::
                project_registered_syscall_request(
                    stop,
                    astraea::execution::
                        GuestRequestId{.value = 11U},
                    astraea::execution::
                        GuestWorkerId{.value = 3U},
                    astraea::execution::
                        GuestThreadId{.value = 7U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Error::stop_has_fault);
    }

    SECTION("zero IDs") {
        const auto stop = registered_stop();

        const auto request_zero =
            astraea::execution::
                project_registered_syscall_request(
                    stop,
                    astraea::execution::
                        GuestRequestId{.value = 0U},
                    astraea::execution::
                        GuestWorkerId{.value = 3U},
                    astraea::execution::
                        GuestThreadId{.value = 7U});
        REQUIRE_FALSE(request_zero.has_value());
        REQUIRE(
            request_zero.error().code ==
            Error::zero_request_id);

        const auto worker_zero =
            astraea::execution::
                project_registered_syscall_request(
                    stop,
                    astraea::execution::
                        GuestRequestId{.value = 11U},
                    astraea::execution::
                        GuestWorkerId{.value = 0U},
                    astraea::execution::
                        GuestThreadId{.value = 7U});
        REQUIRE_FALSE(worker_zero.has_value());
        REQUIRE(
            worker_zero.error().code ==
            Error::zero_worker_id);

        const auto thread_zero =
            astraea::execution::
                project_registered_syscall_request(
                    stop,
                    astraea::execution::
                        GuestRequestId{.value = 11U},
                    astraea::execution::
                        GuestWorkerId{.value = 3U},
                    astraea::execution::
                        GuestThreadId{.value = 0U});
        REQUIRE_FALSE(thread_zero.has_value());
        REQUIRE(
            thread_zero.error().code ==
            Error::zero_thread_id);
    }
}

TEST_CASE(
    "syscall resume validates request identity trap and unsupported errno before mutation",
    "[execution][c0][syscall-context][negative][resume]") {
    using Error =
        astraea::execution::
            GuestWorkerSyscallContextErrorCode;

    const auto stop = registered_stop();
    const auto req = request();

    SECTION("request RIP must match captured stop") {
        auto bad_request = req;
        bad_request.guest_rip =
            astraea::memory::GuestAddress{
                stop.context.rip + 1U};

        const auto resumed =
            astraea::execution::
                apply_registered_syscall_result_to_context(
                    stop,
                    trap(),
                    bad_request,
                    result());

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Error::request_rip_mismatch);
    }

    SECTION("registered site must be the exact syscall trap") {
        auto bad_trap = trap();
        bad_trap.guest_rip =
            astraea::memory::GuestAddress{
                stop.context.rip + 2U};

        const auto resumed =
            astraea::execution::
                apply_registered_syscall_result_to_context(
                    stop,
                    bad_trap,
                    req,
                    result());

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Error::invalid_registered_trap);
    }

    SECTION("protocol identities are validated") {
        auto bad_result = result();
        bad_result.request_id.value = 99U;

        const auto resumed =
            astraea::execution::
                apply_registered_syscall_result_to_context(
                    stop,
                    trap(),
                    req,
                    bad_result);

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Error::protocol_validation_failure);
        REQUIRE(resumed.error().protocol_error.has_value());
        REQUIRE(
            resumed.error().protocol_error->code ==
            astraea::execution::
                GuestWorkerProtocolValidationErrorCode::
                    syscall_request_id_mismatch);
    }

    SECTION("errno semantics are not guessed") {
        auto bad_result = result();
        bad_result.guest_errno = 5;

        const auto resumed =
            astraea::execution::
                apply_registered_syscall_result_to_context(
                    stop,
                    trap(),
                    req,
                    bad_result);

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Error::unsupported_guest_errno);
    }

    SECTION("RIP advance is checked") {
        const auto near_end =
            registered_stop(
                std::numeric_limits<
                    std::uint64_t>::max() - 1U);
        auto near_request = req;
        near_request.guest_rip =
            astraea::memory::GuestAddress{
                near_end.context.rip};
        auto near_trap =
            trap(near_end.context.rip);

        const auto resumed =
            astraea::execution::
                apply_registered_syscall_result_to_context(
                    near_end,
                    near_trap,
                    near_request,
                    result());

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Error::resume_rip_overflow);
    }
}
