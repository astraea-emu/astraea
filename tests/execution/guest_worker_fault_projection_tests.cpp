#include <astraea/execution/guest_worker_fault_projection.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::execution::ExecutionStop fault_stop(
    astraea::execution::GuestFaultKind kind,
    bool has_fault_address,
    std::uint64_t fault_address = 0xfeed0000U) {
    constexpr std::uint64_t kRip = 0x400123U;
    astraea::execution::GuestCpuContext context{};
    context.rip = kRip;

    astraea::execution::ExecutionStop stop{
        .reason =
            astraea::execution::
                ExecutionStopReason::guest_fault,
        .context = context,
        .has_gate_slot = false,
        .gate_slot = 0U,
        .has_fault = true,
        .fault =
            astraea::execution::GuestFault{
                .kind = kind,
                .instruction_pointer = kRip,
                .stack_pointer = 0x800000U,
                .has_fault_address =
                    has_fault_address,
                .fault_address =
                    fault_address,
                .host_code = 0x55U,
            },
    };
    return stop;
}

}  // namespace

TEST_CASE(
    "native access violation projects to typed worker fault",
    "[execution][c0][fault][projection]") {
    const auto projected =
        astraea::execution::
            project_guest_worker_fault(
                fault_stop(
                    astraea::execution::
                        GuestFaultKind::
                            access_violation,
                    true,
                    0U),
                astraea::execution::
                    GuestWorkerId{.value = 1U},
                astraea::execution::
                    GuestThreadId{.value = 7U});

    REQUIRE(projected.has_value());
    REQUIRE(projected->worker_id.value == 1U);
    REQUIRE(projected->thread_id.value == 7U);
    REQUIRE(
        projected->kind ==
        astraea::execution::
            GuestWorkerFaultKind::
                access_violation);
    REQUIRE(projected->guest_rip.value() == 0x400123U);
    REQUIRE(projected->fault_address.value() == 0U);
}

TEST_CASE(
    "native illegal instruction projects without inventing fault address",
    "[execution][c0][fault][projection]") {
    const auto projected =
        astraea::execution::
            project_guest_worker_fault(
                fault_stop(
                    astraea::execution::
                        GuestFaultKind::
                            illegal_instruction,
                    false),
                astraea::execution::
                    GuestWorkerId{.value = 2U},
                astraea::execution::
                    GuestThreadId{.value = 3U});

    REQUIRE(projected.has_value());
    REQUIRE(
        projected->kind ==
        astraea::execution::
            GuestWorkerFaultKind::
                illegal_instruction);
    REQUIRE(projected->fault_address.value() == 0U);
}

TEST_CASE(
    "native fault projection fails closed for malformed or unsupported stops",
    "[execution][c0][fault][projection][negative]") {
    using Code =
        astraea::execution::
            GuestWorkerFaultProjectionErrorCode;

    SECTION("wrong stop reason") {
        auto stop =
            fault_stop(
                astraea::execution::
                    GuestFaultKind::
                        illegal_instruction,
                false);
        stop.reason =
            astraea::execution::
                ExecutionStopReason::host_gate;

        const auto projected =
            astraea::execution::
                project_guest_worker_fault(
                    stop,
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::invalid_stop_reason);
    }

    SECTION("missing captured fault") {
        auto stop =
            fault_stop(
                astraea::execution::
                    GuestFaultKind::
                        illegal_instruction,
                false);
        stop.has_fault = false;

        const auto projected =
            astraea::execution::
                project_guest_worker_fault(
                    stop,
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::missing_fault);
    }

    SECTION("fault RIP must equal captured context RIP") {
        auto stop =
            fault_stop(
                astraea::execution::
                    GuestFaultKind::
                        illegal_instruction,
                false);
        stop.fault.instruction_pointer += 1U;

        const auto projected =
            astraea::execution::
                project_guest_worker_fault(
                    stop,
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::fault_rip_mismatch);
    }

    SECTION("unsupported native class is not generalized") {
        const auto projected =
            astraea::execution::
                project_guest_worker_fault(
                    fault_stop(
                        astraea::execution::
                            GuestFaultKind::
                                arithmetic,
                        false),
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::unsupported_fault_kind);
    }

    SECTION("zero worker identity") {
        const auto projected =
            astraea::execution::
                project_guest_worker_fault(
                    fault_stop(
                        astraea::execution::
                            GuestFaultKind::
                                illegal_instruction,
                        false),
                    astraea::execution::
                        GuestWorkerId{.value = 0U},
                    astraea::execution::
                        GuestThreadId{.value = 1U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::zero_worker_id);
    }

    SECTION("zero thread identity") {
        const auto projected =
            astraea::execution::
                project_guest_worker_fault(
                    fault_stop(
                        astraea::execution::
                            GuestFaultKind::
                                illegal_instruction,
                        false),
                    astraea::execution::
                        GuestWorkerId{.value = 1U},
                    astraea::execution::
                        GuestThreadId{.value = 0U});

        REQUIRE_FALSE(projected.has_value());
        REQUIRE(
            projected.error().code ==
            Code::zero_thread_id);
    }
}
