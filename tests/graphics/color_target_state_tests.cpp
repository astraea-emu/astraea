#include <astraea/graphics/color_target_state.hpp>

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
    std::uint32_t attrib2 = (3U << 14U) | 3U) {
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
                (3U << 14U) | 3U));

    REQUIRE(result.has_value());
    REQUIRE(
        result->base_address ==
        astraea::graphics::GpuVirtualAddress{
            .value = kAddress});
    REQUIRE(result->raw_target_mask == 0x000000afU);
    REQUIRE(result->write_mask == 0x0fU);
    REQUIRE(result->raw_info == kInfo);
    REQUIRE(result->format == 10U);
    REQUIRE(result->number_type == 3U);
    REQUIRE(result->component_swap == 2U);
    REQUIRE(result->dcc_enabled);
    REQUIRE(result->width == 4U);
    REQUIRE(result->height == 4U);
    REQUIRE(result->raw_attrib2 == ((3U << 14U) | 3U));
}

TEST_CASE(
    "Color Target 0 decoder preserves explicitly programmed 1x1 ATTRIB2",
    "[graphics][color-target][context][dimensions]") {
    const auto result =
        astraea::graphics::resolve_color_target0_context_state(
            make_complete_state(
                0x0000000000000100ULL,
                0x0fU,
                10U << 2U,
                0U));

    REQUIRE(result.has_value());
    REQUIRE(result->width == 1U);
    REQUIRE(result->height == 1U);
    REQUIRE(result->raw_attrib2 == 0U);
}

TEST_CASE(
    "Color Target 0 decoder distinguishes every required uninitialized register",
    "[graphics][color-target][context][negative]") {
    SECTION("target mask") {
        auto state = make_complete_state();
        state.initialized.reset(
            astraea::graphics::kColorTargetMaskContextOffset);

        const auto result =
            astraea::graphics::resolve_color_target0_context_state(state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::ColorTarget0ContextErrorCode::
                target_mask_uninitialized);
    }

    SECTION("base") {
        auto state = make_complete_state();
        state.initialized.reset(
            astraea::graphics::kColorTarget0BaseContextOffset);

        const auto result =
            astraea::graphics::resolve_color_target0_context_state(state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::ColorTarget0ContextErrorCode::
                base_uninitialized);
    }

    SECTION("base ext") {
        auto state = make_complete_state();
        state.initialized.reset(
            astraea::graphics::kColorTarget0BaseExtContextOffset);

        const auto result =
            astraea::graphics::resolve_color_target0_context_state(state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::ColorTarget0ContextErrorCode::
                base_ext_uninitialized);
        REQUIRE(result.error().raw_base == 0x3456789aU);
    }

    SECTION("info") {
        auto state = make_complete_state();
        state.initialized.reset(
            astraea::graphics::kColorTarget0InfoContextOffset);

        const auto result =
            astraea::graphics::resolve_color_target0_context_state(state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::ColorTarget0ContextErrorCode::
                info_uninitialized);
    }

    SECTION("attrib2") {
        auto state = make_complete_state();
        state.initialized.reset(
            astraea::graphics::kColorTarget0Attrib2ContextOffset);

        const auto result =
            astraea::graphics::resolve_color_target0_context_state(state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::ColorTarget0ContextErrorCode::
                attrib2_uninitialized);
    }
}

TEST_CASE(
    "Color Target 0 decoder rejects BASE_EXT values that overflow after 256-byte scaling",
    "[graphics][color-target][context][negative][address]") {
    auto state = make_complete_state(
        0x0000000000000100ULL);

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
    REQUIRE(result.error().raw_base == 0xffffffffU);
    REQUIRE(result.error().raw_base_ext == 0x01000000U);
}

TEST_CASE(
    "Color Target 0 decoder preserves disabled and zero-format raw state",
    "[graphics][color-target][context][raw]") {
    const auto result =
        astraea::graphics::resolve_color_target0_context_state(
            make_complete_state(
                0x0000000000000100ULL,
                0U,
                0U,
                (3U << 14U) | 3U));

    REQUIRE(result.has_value());
    REQUIRE(result->write_mask == 0U);
    REQUIRE(result->format == 0U);
    REQUIRE_FALSE(result->dcc_enabled);
    REQUIRE(result->width == 4U);
    REQUIRE(result->height == 4U);
}
