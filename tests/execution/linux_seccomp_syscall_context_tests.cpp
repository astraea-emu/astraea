#include <astraea/execution/linux_seccomp_syscall_context.hpp>

#include <bit>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::execution::LinuxSeccompSyscallTrap trap() {
    astraea::execution::GuestCpuContext context{};
    context.rax = 0x1234U;
    context.rdi = 0x1111111111111111ULL;
    context.rsi = 0x2222222222222222ULL;
    context.rdx = 0x3333333333333333ULL;
    context.r10 = 0x4444444444444444ULL;
    context.r8 = 0x5555555555555555ULL;
    context.r9 = 0x6666666666666666ULL;
    context.rcx = 0x7777777777777777ULL;
    context.r11 = 0x8888888888888888ULL;
    context.rip = 0x400102U;
    context.rflags = 0x202U;

    return astraea::execution::LinuxSeccompSyscallTrap{
        .context = context,
        .guest_rip =
            astraea::memory::GuestAddress{0x400100U},
        .syscall_number = 0x1234,
        .audit_arch =
            astraea::execution::
                kLinuxAuditArchX86_64,
    };
}

astraea::execution::GuestWorkerSyscallRequest request() {
    const auto projected =
        astraea::execution::
            project_linux_seccomp_syscall_request(
                trap(),
                astraea::execution::
                    GuestRequestId{.value = 41U},
                astraea::execution::
                    GuestWorkerId{.value = 1U},
                astraea::execution::
                    GuestThreadId{.value = 1U});
    REQUIRE(projected.has_value());
    return projected.value();
}

astraea::execution::GuestWorkerSyscallResult result() {
    return astraea::execution::GuestWorkerSyscallResult{
        .request_id =
            astraea::execution::
                GuestRequestId{.value = 41U},
        .worker_id =
            astraea::execution::
                GuestWorkerId{.value = 1U},
        .thread_id =
            astraea::execution::
                GuestThreadId{.value = 1U},
        .return_value = -77,
        .guest_errno = 0,
    };
}

}  // namespace

TEST_CASE(
    "Linux seccomp trap projects x86-64 syscall request from captured state",
    "[execution][c0][seccomp][projection]") {
    const auto t = trap();

    const auto projected =
        astraea::execution::
            project_linux_seccomp_syscall_request(
                t,
                astraea::execution::
                    GuestRequestId{.value = 41U},
                astraea::execution::
                    GuestWorkerId{.value = 1U},
                astraea::execution::
                    GuestThreadId{.value = 1U});

    REQUIRE(projected.has_value());
    REQUIRE(projected->request_id.value == 41U);
    REQUIRE(projected->worker_id.value == 1U);
    REQUIRE(projected->thread_id.value == 1U);
    REQUIRE(projected->guest_syscall_number == 0x1234U);
    REQUIRE(projected->arguments[0] == t.context.rdi);
    REQUIRE(projected->arguments[1] == t.context.rsi);
    REQUIRE(projected->arguments[2] == t.context.rdx);
    REQUIRE(projected->arguments[3] == t.context.r10);
    REQUIRE(projected->arguments[4] == t.context.r8);
    REQUIRE(projected->arguments[5] == t.context.r9);
    REQUIRE(projected->guest_rip == t.guest_rip);
}

TEST_CASE(
    "Linux seccomp resume changes only RAX and preserves post-syscall RIP",
    "[execution][c0][seccomp][resume]") {
    const auto t = trap();
    const auto req = request();
    const auto res = result();

    const auto resumed =
        astraea::execution::
            apply_linux_seccomp_syscall_result_to_context(
                t,
                req,
                res);

    REQUIRE(resumed.has_value());

    auto expected = t.context;
    expected.rax =
        std::bit_cast<std::uint64_t>(
            res.return_value);

    REQUIRE(resumed.value() == expected);
    REQUIRE(resumed->rip == t.guest_rip.value() + 2U);
}

TEST_CASE(
    "Linux seccomp projection rejects unsupported ABI and malformed post-call PC",
    "[execution][c0][seccomp][projection][negative]") {
    using Code =
        astraea::execution::
            LinuxSeccompSyscallContextErrorCode;

    SECTION("i386 audit ABI is trapped but not admitted to x86-64 resume profile") {
        auto t = trap();
        t.audit_arch = 0x40000003U;

        const auto projected =
            astraea::execution::
                project_linux_seccomp_syscall_request(
                    t,
                    astraea::execution::
                        GuestRequestId{.value = 41U},
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::unsupported_audit_arch);
    }

    SECTION("captured RIP must already be after the two-byte SYSCALL") {
        auto t = trap();
        t.context.rip = t.guest_rip.value();

        const auto projected =
            astraea::execution::
                project_linux_seccomp_syscall_request(
                    t,
                    astraea::execution::
                        GuestRequestId{.value = 41U},
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::post_syscall_rip_mismatch);
    }

    SECTION("call-site arithmetic is checked") {
        auto t = trap();
        t.guest_rip =
            astraea::memory::GuestAddress{
                std::numeric_limits<
                    std::uint64_t>::max()};
        t.context.rip = 0U;

        const auto projected =
            astraea::execution::
                project_linux_seccomp_syscall_request(
                    t,
                    astraea::execution::
                        GuestRequestId{.value = 41U},
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::call_rip_overflow);
    }
}

TEST_CASE(
    "Linux seccomp resume validates request identity semantics and errno",
    "[execution][c0][seccomp][resume][negative]") {
    using Code =
        astraea::execution::
            LinuxSeccompSyscallContextErrorCode;

    const auto t = trap();
    const auto req = request();

    SECTION("request RIP must match kernel-reported call site") {
        auto bad = req;
        bad.guest_rip =
            astraea::memory::GuestAddress{
                req.guest_rip.value() + 1U};

        const auto resumed =
            astraea::execution::
                apply_linux_seccomp_syscall_result_to_context(
                    t,
                    bad,
                    result());

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Code::request_rip_mismatch);
    }

    SECTION("request arguments must remain captured values") {
        auto bad = req;
        bad.arguments[3] ^= 1U;

        const auto resumed =
            astraea::execution::
                apply_linux_seccomp_syscall_result_to_context(
                    t,
                    bad,
                    result());

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Code::request_argument_mismatch);
    }

    SECTION("controller identity is validated") {
        auto bad_result = result();
        bad_result.thread_id.value += 1U;

        const auto resumed =
            astraea::execution::
                apply_linux_seccomp_syscall_result_to_context(
                    t,
                    req,
                    bad_result);

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Code::protocol_validation_failure);
        REQUIRE(resumed.error().protocol_error.has_value());
    }

    SECTION("errno semantics are not guessed") {
        auto bad_result = result();
        bad_result.guest_errno = 5;

        const auto resumed =
            astraea::execution::
                apply_linux_seccomp_syscall_result_to_context(
                    t,
                    req,
                    bad_result);

        REQUIRE_FALSE(resumed.has_value());
        REQUIRE(
            resumed.error().code ==
            Code::unsupported_guest_errno);
    }
}
