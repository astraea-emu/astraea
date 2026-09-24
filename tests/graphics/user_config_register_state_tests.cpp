#include <astraea/graphics/user_config_register_state.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::GraphicsIrEmission uconfig_write(
    std::uint16_t start,
    std::vector<std::uint32_t> values) {
    return astraea::graphics::GraphicsIrEmission{
        .operation =
            astraea::graphics::
                GraphicsIrUserConfigRegisterWriteRange{
                    .start_offset = start,
                    .values = std::move(values),
                },
        .provenance = {},
    };
}

}  // namespace

TEST_CASE(
    "user-config register state applies and overwrites exact ranges",
    "[graphics][uconfig-state]") {
    astraea::graphics::UserConfigRegisterState state{};

    const auto first =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                uconfig_write(
                    0x242U,
                    {4U, 0x11223344U}),
                state);
    REQUIRE(first.has_value());
    REQUIRE(state.values[0x242U] == 4U);
    REQUIRE(state.values[0x243U] == 0x11223344U);

    const auto second =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                uconfig_write(0x242U, {7U}),
                state);
    REQUIRE(second.has_value());
    REQUIRE(state.values[0x242U] == 7U);
    REQUIRE(state.values[0x243U] == 0x11223344U);
}

TEST_CASE(
    "user-config register state rejects invalid IR atomically",
    "[graphics][uconfig-state][negative]") {
    astraea::graphics::UserConfigRegisterState state{};
    state.values[0x3ffU] = 0x12345678U;
    state.initialized.set(0x3ffU);
    const auto before = state;

    const auto out_of_range =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                uconfig_write(
                    0x3ffU,
                    {0xaaaa5555U, 0xbbbb6666U}),
                state);
    REQUIRE_FALSE(out_of_range.has_value());
    REQUIRE(
        out_of_range.error().code ==
        astraea::graphics::
            UserConfigRegisterApplyErrorCode::
                register_range_out_of_bounds);
    REQUIRE(state == before);

    const astraea::graphics::GraphicsIrEmission unsupported{
        .operation =
            astraea::graphics::GraphicsIrUnsupported{},
        .provenance = {},
    };
    const auto unsupported_result =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                unsupported,
                state);
    REQUIRE_FALSE(unsupported_result.has_value());
    REQUIRE(state == before);
}
