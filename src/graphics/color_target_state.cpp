#include <astraea/graphics/color_target_state.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace astraea::graphics {
namespace {

[[nodiscard]] ColorTarget0ContextError error(
    ColorTarget0ContextErrorCode code,
    std::uint32_t raw_base = 0,
    std::uint32_t raw_base_ext = 0) noexcept {
    return ColorTarget0ContextError{
        .code = code,
        .raw_base = raw_base,
        .raw_base_ext = raw_base_ext,
    };
}

[[nodiscard]] bool initialized(
    const ContextRegisterState& state,
    std::uint16_t offset) noexcept {
    return state.initialized.test(
        static_cast<std::size_t>(offset));
}

[[nodiscard]] std::uint32_t value(
    const ContextRegisterState& state,
    std::uint16_t offset) noexcept {
    return state.values[
        static_cast<std::size_t>(offset)];
}

}  // namespace

ColorTarget0ContextResult
resolve_color_target0_context_state(
    const ContextRegisterState& state) noexcept {
    if (!initialized(state, kColorTargetMaskContextOffset)) {
        return ColorTarget0ContextResult::failure(
            error(ColorTarget0ContextErrorCode::target_mask_uninitialized));
    }

    if (!initialized(state, kColorTarget0BaseContextOffset)) {
        return ColorTarget0ContextResult::failure(
            error(ColorTarget0ContextErrorCode::base_uninitialized));
    }

    const auto raw_base = value(state, kColorTarget0BaseContextOffset);

    if (!initialized(state, kColorTarget0BaseExtContextOffset)) {
        return ColorTarget0ContextResult::failure(
            error(
                ColorTarget0ContextErrorCode::base_ext_uninitialized,
                raw_base));
    }

    const auto raw_base_ext =
        value(state, kColorTarget0BaseExtContextOffset);

    if (!initialized(state, kColorTarget0InfoContextOffset)) {
        return ColorTarget0ContextResult::failure(
            error(
                ColorTarget0ContextErrorCode::info_uninitialized,
                raw_base,
                raw_base_ext));
    }

    if (!initialized(state, kColorTarget0Attrib2ContextOffset)) {
        return ColorTarget0ContextResult::failure(
            error(
                ColorTarget0ContextErrorCode::attrib2_uninitialized,
                raw_base,
                raw_base_ext));
    }

    const auto unshifted =
        (static_cast<std::uint64_t>(raw_base_ext) << 32U) |
        static_cast<std::uint64_t>(raw_base);

    if (unshifted >
        (std::numeric_limits<std::uint64_t>::max() >> 8U)) {
        return ColorTarget0ContextResult::failure(
            error(
                ColorTarget0ContextErrorCode::address_overflow,
                raw_base,
                raw_base_ext));
    }

    const auto raw_target_mask =
        value(state, kColorTargetMaskContextOffset);
    const auto raw_info =
        value(state, kColorTarget0InfoContextOffset);
    const auto raw_attrib2 =
        value(state, kColorTarget0Attrib2ContextOffset);

    return ColorTarget0ContextResult::success(
        ColorTarget0ContextState{
            .base_address =
                GpuVirtualAddress{
                    .value = unshifted << 8U,
                },
            .raw_target_mask = raw_target_mask,
            .write_mask =
                static_cast<std::uint8_t>(
                    raw_target_mask & 0x0fU),
            .raw_info = raw_info,
            .format =
                static_cast<std::uint8_t>(
                    (raw_info >> 2U) & 0x1fU),
            .number_type =
                static_cast<std::uint8_t>(
                    (raw_info >> 8U) & 0x07U),
            .component_swap =
                static_cast<std::uint8_t>(
                    (raw_info >> 11U) & 0x03U),
            .dcc_enabled =
                ((raw_info >> 28U) & 0x01U) != 0U,
            .raw_attrib2 = raw_attrib2,
            .width =
                ((raw_attrib2 >> 14U) & 0x3fffU) + 1U,
            .height =
                (raw_attrib2 & 0x3fffU) + 1U,
        });
}

}  // namespace astraea::graphics
