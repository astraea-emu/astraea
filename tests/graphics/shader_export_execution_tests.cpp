#include <astraea/graphics/shader_export_execution.hpp>
#include <astraea/graphics/shader_cfg.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/shader_wave_program_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kExpBase = 0xf8000000U;
constexpr std::uint32_t kEndPgm = 0xbf810000U;

constexpr std::uint32_t make_exp_word0(
    std::uint8_t enable_mask,
    std::uint8_t target,
    bool done = true) {
    return kExpBase |
           (static_cast<std::uint32_t>(enable_mask) & 0x0fU) |
           ((static_cast<std::uint32_t>(target) & 0x3fU) << 4U) |
           (done ? (1U << 11U) : 0U);
}

constexpr std::uint32_t make_exp_sources(
    std::uint8_t s0,
    std::uint8_t s1,
    std::uint8_t s2,
    std::uint8_t s3) {
    return
        static_cast<std::uint32_t>(s0) |
        (static_cast<std::uint32_t>(s1) << 8U) |
        (static_cast<std::uint32_t>(s2) << 16U) |
        (static_cast<std::uint32_t>(s3) << 24U);
}

astraea::graphics::ShaderIrOperation export_operation() {
    return astraea::graphics::ShaderIrExport{
        .target_kind =
            astraea::graphics::
                ShaderIrExportTargetKind::mrt,
        .target_index = 0U,
        .enable_mask = 0x0fU,
        .compressed = false,
        .done = true,
        .valid_mask = false,
        .sources = {
            astraea::graphics::ShaderIrVgpr{.index = 4U},
            astraea::graphics::ShaderIrVgpr{.index = 5U},
            astraea::graphics::ShaderIrVgpr{.index = 6U},
            astraea::graphics::ShaderIrVgpr{.index = 7U},
        },
    };
}

}  // namespace

TEST_CASE(
    "Shader export capture snapshots active raw source VGPRs without mutation",
    "[graphics][shader-export][capture]") {
    astraea::graphics::ShaderScalarState scalar{};
    scalar.exec = 0b0101U;

    astraea::graphics::ShaderVectorState vector{};
    vector.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;

    for (std::size_t component = 0U;
         component < 4U;
         ++component) {
        const auto vgpr = 4U + component;
        vector.vgprs[vgpr][0] =
            static_cast<std::uint32_t>(
                0x1000U + component);
        vector.vgprs[vgpr][1] =
            static_cast<std::uint32_t>(
                0x2000U + component);
        vector.vgprs[vgpr][2] =
            static_cast<std::uint32_t>(
                0x3000U + component);
    }

    const auto before_scalar = scalar;
    const auto before_vector = vector;

    const auto result =
        astraea::graphics::
            capture_shader_export_operation(
                export_operation(),
                scalar,
                vector);

    REQUIRE(result.has_value());
    REQUIRE(
        result->wave_size ==
        astraea::graphics::ShaderWaveSize::wave32);
    REQUIRE(
        result->target_kind ==
        astraea::graphics::
            ShaderIrExportTargetKind::mrt);
    REQUIRE(result->target_index == 0U);
    REQUIRE(result->enable_mask == 0x0fU);
    REQUIRE(result->done);
    REQUIRE(result->active_lane_mask == 0b0101U);

    for (std::size_t component = 0U;
         component < 4U;
         ++component) {
        REQUIRE(
            result->source_values[component][0] ==
            static_cast<std::uint32_t>(
                0x1000U + component));
        REQUIRE(
            result->source_values[component][1] == 0U);
        REQUIRE(
            result->source_values[component][2] ==
            static_cast<std::uint32_t>(
                0x3000U + component));
    }

    REQUIRE(scalar == before_scalar);
    REQUIRE(vector == before_vector);
}

TEST_CASE(
    "Shader export capture rejects unspecified wave size explicitly",
    "[graphics][shader-export][capture][negative]") {
    astraea::graphics::ShaderScalarState scalar{};
    scalar.exec = 1U;
    astraea::graphics::ShaderVectorState vector{};

    const auto result =
        astraea::graphics::
            capture_shader_export_operation(
                export_operation(),
                scalar,
                vector);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderExportCaptureErrorCode::
                invalid_wave_size);
}

TEST_CASE(
    "wave program execution preserves EXP as a source-order export effect",
    "[graphics][shader-export][wave-program]") {
    const std::array<std::uint32_t, 3> words{
        make_exp_word0(
            0x0fU,
            0x00U),
        make_exp_sources(
            4U,
            5U,
            6U,
            7U),
        kEndPgm,
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());
    REQUIRE(graph.has_value());

    astraea::graphics::ShaderScalarState scalar{};
    scalar.exec = 1U;

    astraea::graphics::ShaderVectorState vector{};
    vector.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    vector.vgprs[4U][0U] = 0x3f800000U;
    vector.vgprs[5U][0U] = 0x00000000U;
    vector.vgprs[6U][0U] = 0x3f800000U;
    vector.vgprs[7U][0U] = 0x3f800000U;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0U,
                1U,
                scalar,
                vector);

    REQUIRE(result.has_value());
    REQUIRE(result->block_executions.size() == 1U);
    const auto& block =
        result->block_executions.front();
    REQUIRE(block.executed_emission_count == 2U);
    REQUIRE(block.effects.size() == 1U);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::
                ShaderExportCaptureEffect>(
            block.effects.front()));

    const auto& effect =
        std::get<
            astraea::graphics::
                ShaderExportCaptureEffect>(
            block.effects.front());
    REQUIRE(effect.active_lane_mask == 1U);
    REQUIRE(effect.target_index == 0U);
    REQUIRE(
        effect.source_values[0U][0U] ==
        0x3f800000U);
    REQUIRE(
        effect.source_values[1U][0U] ==
        0x00000000U);
    REQUIRE(
        effect.source_values[2U][0U] ==
        0x3f800000U);
    REQUIRE(
        effect.source_values[3U][0U] ==
        0x3f800000U);
}
