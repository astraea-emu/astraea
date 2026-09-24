#include <astraea/execution/registered_syscall_trap.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

std::vector<std::byte> owned_code() {
    return {
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x0f},
        std::byte{0x05},
        std::byte{0x90},
        std::byte{0xc3},
    };
}

}  // namespace

TEST_CASE(
    "registered syscall trap patches and restores one exact owned site",
    "[execution][c0][syscall-trap]") {
    auto code = owned_code();
    const auto original = code;

    const auto plan =
        astraea::execution::
            plan_registered_syscall_trap(
                astraea::memory::GuestAddress{
                    0x400000U},
                code,
                astraea::memory::GuestAddress{
                    0x400002U});

    REQUIRE(plan.has_value());
    REQUIRE(plan->code_byte_offset == 2U);
    REQUIRE(
        astraea::execution::
            registered_syscall_trap_matches_rip(
                plan.value(),
                astraea::memory::GuestAddress{
                    0x400002U}));
    REQUIRE_FALSE(
        astraea::execution::
            registered_syscall_trap_matches_rip(
                plan.value(),
                astraea::memory::GuestAddress{
                    0x400003U}));

    REQUIRE(
        astraea::execution::
            apply_registered_syscall_trap(
                code,
                plan.value())
            .has_value());
    REQUIRE(code[2] == std::byte{0x0f});
    REQUIRE(code[3] == std::byte{0x0b});
    REQUIRE(code[0] == original[0]);
    REQUIRE(code[1] == original[1]);
    REQUIRE(code[4] == original[4]);
    REQUIRE(code[5] == original[5]);

    REQUIRE(
        astraea::execution::
            restore_registered_syscall_trap(
                code,
                plan.value())
            .has_value());
    REQUIRE(code == original);
}

TEST_CASE(
    "registered syscall trap accepts byte-addressable odd RIP",
    "[execution][c0][syscall-trap][x86]") {
    std::vector<std::byte> code{
        std::byte{0x90},
        std::byte{0x0f},
        std::byte{0x05},
        std::byte{0xc3},
    };

    const auto plan =
        astraea::execution::
            plan_registered_syscall_trap(
                astraea::memory::GuestAddress{
                    0x500000U},
                code,
                astraea::memory::GuestAddress{
                    0x500001U});

    REQUIRE(plan.has_value());
    REQUIRE(plan->code_byte_offset == 1U);
}

TEST_CASE(
    "registered syscall trap rejects wrong original bytes without mutation",
    "[execution][c0][syscall-trap][negative]") {
    auto code = owned_code();
    code[3] = std::byte{0x04};
    const auto before = code;

    const auto plan =
        astraea::execution::
            plan_registered_syscall_trap(
                astraea::memory::GuestAddress{
                    0x400000U},
                code,
                astraea::memory::GuestAddress{
                    0x400002U});

    REQUIRE_FALSE(plan.has_value());
    REQUIRE(
        plan.error().code ==
        astraea::execution::
            RegisteredSyscallTrapErrorCode::
                unexpected_original_bytes);
    REQUIRE(code == before);
}

TEST_CASE(
    "registered syscall trap rejects range errors without mutation",
    "[execution][c0][syscall-trap][negative][range]") {
    const auto code = owned_code();

    SECTION("before range") {
        const auto result =
            astraea::execution::
                plan_registered_syscall_trap(
                    astraea::memory::GuestAddress{
                        0x400000U},
                    code,
                    astraea::memory::GuestAddress{
                        0x3fffffU});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                RegisteredSyscallTrapErrorCode::
                    site_before_code_range);
    }

    SECTION("not fully in range") {
        const auto result =
            astraea::execution::
                plan_registered_syscall_trap(
                    astraea::memory::GuestAddress{
                        0x400000U},
                    code,
                    astraea::memory::GuestAddress{
                        0x400005U});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                RegisteredSyscallTrapErrorCode::
                    site_not_fully_in_code_range);
    }

    SECTION("code range overflow") {
        const auto result =
            astraea::execution::
                plan_registered_syscall_trap(
                    astraea::memory::GuestAddress{
                        std::numeric_limits<
                            std::uint64_t>::max() - 2U},
                    code,
                    astraea::memory::GuestAddress{
                        std::numeric_limits<
                            std::uint64_t>::max() - 1U});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                RegisteredSyscallTrapErrorCode::
                    code_range_overflow);
    }
}

TEST_CASE(
    "registered syscall trap mutation validates complete current state first",
    "[execution][c0][syscall-trap][negative][atomic]") {
    auto code = owned_code();
    const auto plan =
        astraea::execution::
            plan_registered_syscall_trap(
                astraea::memory::GuestAddress{
                    0x400000U},
                code,
                astraea::memory::GuestAddress{
                    0x400002U});
    REQUIRE(plan.has_value());

    code[3] = std::byte{0x04};
    const auto before_apply = code;
    const auto apply =
        astraea::execution::
            apply_registered_syscall_trap(
                code,
                plan.value());
    REQUIRE_FALSE(apply.has_value());
    REQUIRE(code == before_apply);

    code = owned_code();
    REQUIRE(
        astraea::execution::
            apply_registered_syscall_trap(
                code,
                plan.value())
            .has_value());
    code[3] = std::byte{0xcc};
    const auto before_restore = code;

    const auto restore =
        astraea::execution::
            restore_registered_syscall_trap(
                code,
                plan.value());
    REQUIRE_FALSE(restore.has_value());
    REQUIRE(
        restore.error().code ==
        astraea::execution::
            RegisteredSyscallTrapErrorCode::
                unexpected_trap_bytes);
    REQUIRE(code == before_restore);
}
