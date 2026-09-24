#include <astraea/graphics/color_target_state.hpp>

#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

void set_register(
    astraea::graphics::ContextRegisterState& state,
    std::uint16_t offset,
    std::uint32_t value) {
    state.values[offset] = value;
    state.initialized.set(offset);
}

astraea::graphics::ContextRegisterState
make_complete_state(
    std::uint64_t address = 0x0000123456789a00ULL,
    std::uint32_t target_mask = 0x0fU,
    std::uint32_t info = (10U << 2U),
    std::uint32_t attrib2 = (3U << 14U) | 3U,
    std::uint32_t attrib3 = 0U) {
    astraea::graphics::ContextRegisterState state{};

    set_register(
        state,
        astraea::graphics::kColorTargetMaskContextOffset,
        target_mask);
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseContextOffset,
        static_cast<std::uint32_t>(
            (address >> 8U) & 0xffffffffULL));
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseExtContextOffset,
        static_cast<std::uint32_t>(address >> 40U));
    set_register(
        state,
        astraea::graphics::kColorTarget0InfoContextOffset,
        info);
    set_register(
        state,
        astraea::graphics::kColorTarget0Attrib2ContextOffset,
        attrib2);
    set_register(
        state,
        astraea::graphics::kColorTarget0Attrib3ContextOffset,
        attrib3);

    return state;
}

}  // namespace

TEST_CASE(
    "Color Target 0 decoder reconstructs raw 4x4 state exactly",
    "[graphics][color-target][context]") {
    constexpr std::uint64_t kAddress =
        0x0000123456789a00ULL;
    constexpr std::uint32_t kInfo =
        (10U << 2U) |
        (3U << 8U) |
        (2U << 11U) |
        (1U << 28U);

    const auto result =
        astraea::graphics::resolve_color_target0_context_state(
            make_complete_state(
                kAddress,
                0x000000afU,
                kInfo,
                (3U << 14U) | 3U,
                (7U << 14U) | (2U << 24U)));

    REQUIRE(result.has_value());
    REQUIRE(result->base_address.value == kAddress);
    REQUIRE(result->raw_target_mask == 0x000000afU);
    REQUIRE(result->write_mask == 0x0fU);
    REQUIRE(result->format == 10U);
    REQUIRE(result->number_type == 3U);
    REQUIRE(result->component_swap == 2U);
    REQUIRE(result->dcc_enabled);
    REQUIRE(result->width == 4U);
    REQUIRE(result->height == 4U);
    REQUIRE(result->color_sw_mode == 7U);
    REQUIRE(result->resource_type == 2U);
}

TEST_CASE(
    "Color Target 0 decoder requires every source register to be initialized",
    "[graphics][color-target][context][negative]") {
    const std::uint16_t offsets[] = {
        astraea::graphics::kColorTargetMaskContextOffset,
        astraea::graphics::kColorTarget0BaseContextOffset,
        astraea::graphics::kColorTarget0BaseExtContextOffset,
        astraea::graphics::kColorTarget0InfoContextOffset,
        astraea::graphics::kColorTarget0Attrib2ContextOffset,
        astraea::graphics::kColorTarget0Attrib3ContextOffset,
    };
    const astraea::graphics::ColorTarget0ContextErrorCode errors[] = {
        astraea::graphics::ColorTarget0ContextErrorCode::target_mask_uninitialized,
        astraea::graphics::ColorTarget0ContextErrorCode::base_uninitialized,
        astraea::graphics::ColorTarget0ContextErrorCode::base_ext_uninitialized,
        astraea::graphics::ColorTarget0ContextErrorCode::info_uninitialized,
        astraea::graphics::ColorTarget0ContextErrorCode::attrib2_uninitialized,
        astraea::graphics::ColorTarget0ContextErrorCode::attrib3_uninitialized,
    };

    for (std::size_t index = 0; index < 6U; ++index) {
        auto state = make_complete_state();
        state.initialized.reset(offsets[index]);

        const auto result =
            astraea::graphics::resolve_color_target0_context_state(state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == errors[index]);
    }
}

TEST_CASE(
    "Color Target 0 decoder rejects BASE_EXT overflow after scaling",
    "[graphics][color-target][context][negative][address]") {
    auto state = make_complete_state(0x100ULL);
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseContextOffset,
        0xffffffffU);
    set_register(
        state,
        astraea::graphics::kColorTarget0BaseExtContextOffset,
        0x01000000U);

    const auto result =
        astraea::graphics::resolve_color_target0_context_state(state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::ColorTarget0ContextErrorCode::
            address_overflow);
}

TEST_CASE(
    "Color Target 0 decoder preserves disabled and zero-format raw state",
    "[graphics][color-target][context][raw]") {
    const auto result =
        astraea::graphics::resolve_color_target0_context_state(
            make_complete_state(
                0x100ULL,
                0U,
                0U,
                (3U << 14U) | 3U));

    REQUIRE(result.has_value());
    REQUIRE(result->write_mask == 0U);
    REQUIRE(result->format == 0U);
    REQUIRE_FALSE(result->dcc_enabled);
    REQUIRE(result->width == 4U);
    REQUIRE(result->height == 4U);
    REQUIRE(result->raw_attrib3 == 0U);
    REQUIRE(result->color_sw_mode == 0U);
    REQUIRE(result->resource_type == 0U);
}
