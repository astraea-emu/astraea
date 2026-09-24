#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_vector_execution.hpp>

namespace astraea::graphics {

struct ShaderExportCaptureEffect {
    ShaderWaveSize wave_size = ShaderWaveSize::unspecified;
    ShaderIrExportTargetKind target_kind =
        ShaderIrExportTargetKind::mrt;
    std::uint8_t target_index = 0;
    std::uint8_t enable_mask = 0;
    bool compressed = false;
    bool done = false;
    bool valid_mask = false;
    std::uint64_t active_lane_mask = 0;

    // Raw source VGPR bit patterns at the export point, indexed
    // [component/source][lane]. Disabled components are still snapshotted;
    // enable_mask remains authoritative metadata for later interpretation.
    std::array<
        std::array<std::uint32_t, kShaderMaxWaveLaneCount>,
        4>
        source_values{};

    auto operator<=>(const ShaderExportCaptureEffect&) const = default;
};

enum class ShaderExportCaptureErrorCode {
    unsupported_operation,
    invalid_wave_size,
};

struct ShaderExportCaptureError {
    ShaderExportCaptureErrorCode code =
        ShaderExportCaptureErrorCode::unsupported_operation;

    auto operator<=>(const ShaderExportCaptureError&) const = default;
};

using ShaderExportCaptureResult =
    astraea::core::Result<
        ShaderExportCaptureEffect,
        ShaderExportCaptureError>;

// Captures one already-typed ShaderIrExport against explicit generic wave
// state. This operation never mutates scalar or vector state.
//
// The effect deliberately records raw source VGPR values and export metadata;
// compressed packing, render-target conversion, PS5 stage ABI, and final
// hardware export behavior remain later dependencies.
[[nodiscard]] ShaderExportCaptureResult
capture_shader_export_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    const ShaderVectorState& vector_state) noexcept;

}  // namespace astraea::graphics
