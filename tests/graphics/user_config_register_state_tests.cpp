#include <astraea/graphics/user_config_register_state.hpp>

#include <cstdint>
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

    auto first =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                uconfig_write(
                    0x242U,
                    {4U, 0x11223344U}),
                state);
    REQUIRE(first.has_value());
    REQUIRE(first->start_offset == 0x242U);
    REQUIRE(first->value_count == 2U);
    REQUIRE(state.initialized.test(0x242U));
    REQUIRE(state.initialized.test(0x243U));
    REQUIRE(state.values[0x242U] == 4U);
    REQUIRE(state.values[0x243U] == 0x11223344U);

    auto second =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                uconfig_write(
                    0x242U,
                    {7U}),
                state);
    REQUIRE(second.has_value());
    REQUIRE(state.values[0x242U] == 7U);
    REQUIRE(state.values[0x243U] == 0x11223344U);
}

TEST_CASE(
    "user-config state rejects non-uconfig operations without mutation",
    "[graphics][uconfig-state][negative]") {
    astraea::graphics::UserConfigRegisterState state{};
    state.values[0x25bU] = 0xfeedfaceU;
    state.initialized.set(0x25bU);
    const auto before = state;

    const astraea::graphics::GraphicsIrEmission emission{
        .operation =
            astraea::graphics::
                GraphicsIrContextRegisterWriteRange{
                    .start_offset = 1U,
                    .values = {2U},
                },
        .provenance = {},
    };

    const auto result =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                emission,
                state);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            UserConfigRegisterApplyErrorCode::
                unsupported_operation);
    REQUIRE(state == before);
}

TEST_CASE(
    "user-config state reports explicit unsupported IR reason",
    "[graphics][uconfig-state][unsupported]") {
    astraea::graphics::UserConfigRegisterState state{};
    const auto before = state;

    const astraea::graphics::GraphicsIrEmission emission{
        .operation =
            astraea::graphics::GraphicsIrUnsupported{
                .reason =
                    astraea::graphics::
                        GraphicsIrUnsupportedReason::
                            packet_semantics_unknown,
            },
        .provenance = {},
    };

    const auto result =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                emission,
                state);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().unsupported_reason.has_value());
    REQUIRE(
        result.error().unsupported_reason.value() ==
        astraea::graphics::
            GraphicsIrUnsupportedReason::
                packet_semantics_unknown);
    REQUIRE(state == before);
}

TEST_CASE(
    "user-config state validates complete range before mutation",
    "[graphics][uconfig-state][atomic][boundary]") {
    astraea::graphics::UserConfigRegisterState state{};
    state.values[0x3ffU] = 0x12345678U;
    state.initialized.set(0x3ffU);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_user_config_register_graphics_ir(
                uconfig_write(
                    0x3ffU,
                    {0xaaaa5555U, 0xbbbb6666U}),
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            UserConfigRegisterApplyErrorCode::
                register_range_out_of_bounds);
    REQUIRE(result.error().start_offset == 0x3ffU);
    REQUIRE(result.error().value_count == 2U);
    REQUIRE(state == before);
}
