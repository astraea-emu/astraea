#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/graphics/shader_program.hpp>

namespace astraea::graphics {

enum class ShaderExportProbeStage {
    pre_raster,
    fragment,
};

enum class ShaderExportProbeValueSourceKind {
    vertex_index_position_x,
    vertex_index_position_y,
    constant_f32_bits,
};

struct ShaderExportProbeValueSource {
    ShaderExportProbeValueSourceKind kind =
        ShaderExportProbeValueSourceKind::constant_f32_bits;
    std::uint32_t constant_bits = 0;

    auto operator<=>(const ShaderExportProbeValueSource&) const = default;
};

struct ShaderExportProbeVgprBinding {
    ShaderIrVgpr vgpr;
    ShaderExportProbeValueSource source;

    auto operator<=>(const ShaderExportProbeVgprBinding&) const = default;
};

// Explicit synthetic launch bindings for one owned proof workload. These are
// compiler inputs, not a PS5 stage-entry ABI.
struct ShaderExportProbeLaunchAbi {
    ShaderExportProbeStage stage =
        ShaderExportProbeStage::pre_raster;
    std::array<ShaderExportProbeVgprBinding, 4> bindings{};

    auto operator<=>(const ShaderExportProbeLaunchAbi&) const = default;
};

struct ShaderExportProbeCompilerValue {
    ShaderIrVgpr source_vgpr;
    ShaderExportProbeValueSource source;

    auto operator<=>(const ShaderExportProbeCompilerValue&) const = default;
};

// The first workload-driven compiler/value IR: one four-component export with
// provenance back to the semantic Shader IR emission. It intentionally has no
// generic CFG/SSA/resource model because the owned EXP+END workload does not
// require one.
struct ShaderExportProbeCompilerIr {
    ShaderExportProbeStage stage =
        ShaderExportProbeStage::pre_raster;
    std::array<ShaderExportProbeCompilerValue, 4> components{};
    ShaderIrProvenance export_provenance;

    auto operator<=>(const ShaderExportProbeCompilerIr&) const = default;
};

enum class ShaderExportProbeCompilerErrorCode {
    invalid_program_shape,
    unsupported_export_form,
    unsupported_export_target,
    duplicate_binding,
    missing_binding,
    unsupported_probe_binding,
    host_allocation_failure,
};

struct ShaderExportProbeCompilerError {
    ShaderExportProbeCompilerErrorCode code =
        ShaderExportProbeCompilerErrorCode::invalid_program_shape;
    std::size_t component_index = 0;
    std::uint8_t vgpr_index = 0;

    auto operator<=>(const ShaderExportProbeCompilerError&) const = default;
};

using ShaderExportProbeCompilerResult =
    astraea::core::Result<
        ShaderExportProbeCompilerIr,
        ShaderExportProbeCompilerError>;

struct ShaderExportProbeSpirvModule {
    ShaderExportProbeStage stage =
        ShaderExportProbeStage::pre_raster;
    std::vector<std::uint32_t> words;

    auto operator<=>(const ShaderExportProbeSpirvModule&) const = default;
};

using ShaderExportProbeSpirvResult =
    astraea::core::Result<
        ShaderExportProbeSpirvModule,
        ShaderExportProbeCompilerError>;

[[nodiscard]] ShaderExportProbeLaunchAbi
make_fullscreen_position_probe_launch_abi() noexcept;

[[nodiscard]] ShaderExportProbeLaunchAbi
make_magenta_fragment_probe_launch_abi() noexcept;

// Accepts only the exact owned straight-line EXP+END proof profile. Unsupported
// semantic operations or export forms fail explicitly rather than acquiring
// guessed stage semantics.
[[nodiscard]] ShaderExportProbeCompilerResult
compile_shader_export_probe(
    const ShaderIrProgram& program,
    const ShaderExportProbeLaunchAbi& launch_abi);

// Lowers the bounded compiler/value IR to one Vulkan-1.3-compatible SPIR-V 1.6
// stage module. No Vulkan handles or PS5 ABI state enter the compiler IR.
[[nodiscard]] ShaderExportProbeSpirvResult
lower_shader_export_probe_to_spirv(
    const ShaderExportProbeCompilerIr& compiler_ir);

}  // namespace astraea::graphics
