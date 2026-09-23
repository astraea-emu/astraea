#pragma once

#include <array>
#include <bitset>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_set_sh_reg_ir.hpp>

namespace astraea::graphics {

inline constexpr std::size_t kShaderRegisterStateDwords =
    kPm4ShaderRegisterWindowDwords;

inline constexpr std::uint16_t kPixelProgramLoRegisterOffset = 0x0008U;
inline constexpr std::uint16_t kPixelProgramHiRegisterOffset = 0x0009U;

// Evidence-bounded type-2 Geometry/fused-pre-raster profile. These are the
// leading ES PGM_LO/HI registers already required by CreateShader preparation.
inline constexpr std::uint16_t kGeometryEsProgramLoRegisterOffset = 0x00c8U;
inline constexpr std::uint16_t kGeometryEsProgramHiRegisterOffset = 0x00c9U;

struct ShaderRegisterState {
    std::array<std::uint32_t, kShaderRegisterStateDwords> values{};
    std::bitset<kShaderRegisterStateDwords> initialized{};

    bool operator==(const ShaderRegisterState&) const = default;
};

struct ShaderRegisterWriteEffect {
    std::uint16_t start_offset = 0;
    std::size_t value_count = 0;

    auto operator<=>(const ShaderRegisterWriteEffect&) const = default;
};

enum class ShaderRegisterApplyErrorCode {
    unsupported_operation,
    register_range_out_of_bounds,
};

struct ShaderRegisterApplyError {
    ShaderRegisterApplyErrorCode code =
        ShaderRegisterApplyErrorCode::unsupported_operation;
    std::uint16_t start_offset = 0;
    std::size_t value_count = 0;
    std::optional<GraphicsIrUnsupportedReason> unsupported_reason;

    auto operator<=>(const ShaderRegisterApplyError&) const = default;
};

using ShaderRegisterApplyResult =
    astraea::core::Result<
        ShaderRegisterWriteEffect,
        ShaderRegisterApplyError>;

// Applies one Graphics IR shader-register range atomically.
//
// The complete destination extent is validated before any state mutation.
// Unsupported Graphics IR never mutates the state.
[[nodiscard]] ShaderRegisterApplyResult
apply_shader_register_graphics_ir(
    const GraphicsIrEmission& emission,
    ShaderRegisterState& state) noexcept;

struct PixelProgramGpuAddress {
    std::uint64_t value = 0;

    auto operator<=>(const PixelProgramGpuAddress&) const = default;
};

enum class PixelProgramAddressErrorCode {
    pgm_lo_uninitialized,
    pgm_hi_uninitialized,
    unsupported_pgm_hi_bits,
};

struct PixelProgramAddressError {
    PixelProgramAddressErrorCode code =
        PixelProgramAddressErrorCode::pgm_lo_uninitialized;
    std::uint32_t pgm_lo = 0;
    std::uint32_t pgm_hi = 0;

    auto operator<=>(const PixelProgramAddressError&) const = default;
};

using PixelProgramAddressResult =
    astraea::core::Result<
        PixelProgramGpuAddress,
        PixelProgramAddressError>;

// Resolves only the already-evidenced V1 pixel-program register profile.
//
// Relative shader-register offsets 0x08/0x09 encode a 256-byte-aligned GPU
// virtual address as LO bits 8..39 and the low byte of HI as bits 40..47.
// This returns a GPU-domain value, not a CPU guest pointer or Vulkan object.
[[nodiscard]] PixelProgramAddressResult
resolve_pixel_program_address(
    const ShaderRegisterState& state) noexcept;

struct GeometryEsProgramGpuAddress {
    std::uint64_t value = 0;

    auto operator<=>(const GeometryEsProgramGpuAddress&) const = default;
};

enum class GeometryEsProgramAddressErrorCode {
    pgm_lo_uninitialized,
    pgm_hi_uninitialized,
    unsupported_pgm_hi_bits,
};

struct GeometryEsProgramAddressError {
    GeometryEsProgramAddressErrorCode code =
        GeometryEsProgramAddressErrorCode::
            pgm_lo_uninitialized;
    std::uint32_t pgm_lo = 0;
    std::uint32_t pgm_hi = 0;

    auto operator<=>(const GeometryEsProgramAddressError&) const =
        default;
};

using GeometryEsProgramAddressResult =
    astraea::core::Result<
        GeometryEsProgramGpuAddress,
        GeometryEsProgramAddressError>;

// Resolves the exact type-2 Geometry/ES program pair already supported by
// CreateShader preparation.
//
// Relative SH offsets 0xc8/0xc9 use the same evidenced 256-byte program-address
// encoding as the Pixel pair. No ordinary GS pair or other pre-raster stage is
// inferred here.
[[nodiscard]] GeometryEsProgramAddressResult
resolve_geometry_es_program_address(
    const ShaderRegisterState& state) noexcept;

}  // namespace astraea::graphics
