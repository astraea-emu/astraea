#pragma once

#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/context_register_state.hpp>
#include <astraea/graphics/gpu_address.hpp>

namespace astraea::graphics {

// Relative SET_CONTEXT_REG offsets for the first gfx10/Prospero color target.
// The absolute public register identities are:
//
//   CB_TARGET_MASK     0xa08e
//   CB_COLOR0_BASE     0xa318
//   CB_COLOR0_INFO     0xa31c
//   CB_COLOR0_BASE_EXT 0xa390
//   CB_COLOR0_ATTRIB2  0xa3b0
//   CB_COLOR0_ATTRIB3  0xa3b8
//
// Context-register IR stores offsets relative to the 0xa000 context window.
inline constexpr std::uint16_t
    kColorTargetMaskContextOffset = 0x008eU;
inline constexpr std::uint16_t
    kColorTarget0BaseContextOffset = 0x0318U;
inline constexpr std::uint16_t
    kColorTarget0InfoContextOffset = 0x031cU;
inline constexpr std::uint16_t
    kColorTarget0BaseExtContextOffset = 0x0390U;
inline constexpr std::uint16_t
    kColorTarget0Attrib2ContextOffset = 0x03b0U;
inline constexpr std::uint16_t
    kColorTarget0Attrib3ContextOffset = 0x03b8U;

struct ColorTarget0ContextState {
    GpuVirtualAddress base_address;

    std::uint32_t raw_target_mask = 0;
    std::uint8_t write_mask = 0;

    std::uint32_t raw_info = 0;
    std::uint8_t format = 0;
    std::uint8_t number_type = 0;
    std::uint8_t component_swap = 0;
    bool dcc_enabled = false;

    std::uint32_t raw_attrib2 = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    std::uint32_t raw_attrib3 = 0;
    std::uint8_t color_sw_mode = 0;
    std::uint8_t resource_type = 0;

    auto operator<=>(const ColorTarget0ContextState&) const = default;
};

enum class ColorTarget0ContextErrorCode {
    target_mask_uninitialized,
    base_uninitialized,
    base_ext_uninitialized,
    info_uninitialized,
    attrib2_uninitialized,
    attrib3_uninitialized,
    address_overflow,
};

struct ColorTarget0ContextError {
    ColorTarget0ContextErrorCode code =
        ColorTarget0ContextErrorCode::target_mask_uninitialized;

    std::uint32_t raw_base = 0;
    std::uint32_t raw_base_ext = 0;

    auto operator<=>(const ColorTarget0ContextError&) const = default;
};

using ColorTarget0ContextResult =
    astraea::core::Result<
        ColorTarget0ContextState,
        ColorTarget0ContextError>;

// Decodes only raw Color Target 0 state from an already-applied context
// register snapshot.
//
// This function intentionally does not:
// - decide whether the target is enabled or writable;
// - assign a Vulkan/host format;
// - interpret the decoded ATTRIB3 swizzle/resource-type values;
// - resolve the GPU address to backing storage;
// - handle DCC metadata.
//
// BASE/BASE_EXT reconstruct the guest GPU VA as
// ((BASE_EXT << 32) | BASE) << 8. ATTRIB2 stores MIP0_HEIGHT-1 in [13:0]
// and MIP0_WIDTH-1 in [27:14]. ATTRIB3 exposes COLOR_SW_MODE in [18:14]
// and RESOURCE_TYPE in [25:24].
[[nodiscard]] ColorTarget0ContextResult
resolve_color_target0_context_state(
    const ContextRegisterState& state) noexcept;

}  // namespace astraea::graphics
