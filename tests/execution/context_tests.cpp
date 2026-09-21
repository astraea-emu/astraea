#include <astraea/execution/backend.hpp>
#include <astraea/execution/context.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("synthetic execution context is deterministic and minimal", "[execution][context]") {
    const auto context = astraea::execution::make_synthetic_initial_context(
        0x401000,
        astraea::memory::GuestAddress{0x7fff1230});

    REQUIRE(context.rip == 0x401000);
    REQUIRE(context.rsp == 0x7fff1230);
    REQUIRE(context.rflags == astraea::execution::kSyntheticInitialRflags);
    REQUIRE((context.rflags & (std::uint64_t{1} << 10U)) == 0);

    REQUIRE(context.rax == 0);
    REQUIRE(context.rbx == 0);
    REQUIRE(context.rcx == 0);
    REQUIRE(context.rdx == 0);
    REQUIRE(context.rsi == 0);
    REQUIRE(context.rdi == 0);
    REQUIRE(context.rbp == 0);
    REQUIRE(context.r8 == 0);
    REQUIRE(context.r9 == 0);
    REQUIRE(context.r10 == 0);
    REQUIRE(context.r11 == 0);
    REQUIRE(context.r12 == 0);
    REQUIRE(context.r13 == 0);
    REQUIRE(context.r14 == 0);
    REQUIRE(context.r15 == 0);
    REQUIRE(context.fs_base == 0);
    REQUIRE(context.gs_base == 0);
}


TEST_CASE("native backend errors preserve portable category and diagnostics", "[execution][backend]") {
    const astraea::execution::NativeBackendError error{
        .code = astraea::execution::NativeBackendErrorCode::guest_address_unavailable,
        .has_guest_address = true,
        .guest_address = 0x400000,
        .has_host_code = true,
        .host_code = 17,
    };

    REQUIRE(
        error.code ==
        astraea::execution::NativeBackendErrorCode::guest_address_unavailable);
    REQUIRE(error.has_guest_address);
    REQUIRE(error.guest_address == 0x400000);
    REQUIRE(error.has_host_code);
    REQUIRE(error.host_code == 17);
}
