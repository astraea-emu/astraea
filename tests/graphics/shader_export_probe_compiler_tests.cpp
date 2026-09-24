#include <astraea/graphics/shader_export_execution.hpp>
#include <astraea/graphics/shader_export_probe_compiler.hpp>
#include <astraea/graphics/shader_program.hpp>

#include <array>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <spirv-tools/libspirv.hpp>

namespace {

constexpr std::uint32_t kExpBase = 0xf8000000U;
constexpr std::uint32_t kEndPgm = 0xbf810000U;

constexpr std::uint32_t make_exp_word0(
    std::uint8_t target,
    bool compressed = false) {
    return kExpBase |
           0x0fU |
           ((static_cast<std::uint32_t>(target) & 0x3fU) << 4U) |
           (compressed ? (1U << 10U) : 0U) |
           (1U << 11U);
}

constexpr std::uint32_t make_exp_sources(
    std::uint8_t s0 = 0U,
    std::uint8_t s1 = 1U,
    std::uint8_t s2 = 2U,
    std::uint8_t s3 = 3U) {
    return
        static_cast<std::uint32_t>(s0) |
        (static_cast<std::uint32_t>(s1) << 8U) |
        (static_cast<std::uint32_t>(s2) << 16U) |
        (static_cast<std::uint32_t>(s3) << 24U);
}

astraea::graphics::ShaderIrProgram
owned_export_program(std::uint8_t target) {
    const std::array<std::uint32_t, 3> words{
        make_exp_word0(target),
        make_exp_sources(),
        kEndPgm,
    };
    const auto result =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(result.has_value());
    return result.value();
}

std::string validate_and_disassemble(
    const std::vector<std::uint32_t>& words) {
    spvtools::SpirvTools tools{
        SPV_ENV_VULKAN_1_3};
    REQUIRE(tools.IsValid());

    std::string diagnostics;
    tools.SetMessageConsumer(
        [&diagnostics](
            spv_message_level_t,
            const char*,
            const spv_position_t&,
            const char* message) {
            if (message != nullptr) {
                diagnostics += message;
                diagnostics.push_back('\n');
            }
        });

    const bool valid = tools.Validate(words);
    INFO(diagnostics);
    REQUIRE(valid);

    std::string text;
    REQUIRE(tools.Disassemble(words, &text));
    return text;
}

}  // namespace

TEST_CASE(
    "owned Position0 export becomes bounded compiler IR with provenance",
    "[graphics][compiler][export-probe][vertex]") {
    const auto program =
        owned_export_program(0x0cU);
    const auto launch =
        astraea::graphics::
            make_fullscreen_position_probe_launch_abi();

    const auto compiled =
        astraea::graphics::
            compile_shader_export_probe(
                program,
                launch);

    REQUIRE(compiled.has_value());
    REQUIRE(
        compiled->stage ==
        astraea::graphics::
            ShaderExportProbeStage::pre_raster);
    REQUIRE(compiled->components[0].source_vgpr.index == 0U);
    REQUIRE(compiled->components[1].source_vgpr.index == 1U);
    REQUIRE(compiled->components[2].source_vgpr.index == 2U);
    REQUIRE(compiled->components[3].source_vgpr.index == 3U);
    REQUIRE(
        compiled->components[0].source.kind ==
        astraea::graphics::
            ShaderExportProbeValueSourceKind::
                vertex_index_position_x);
    REQUIRE(
        compiled->components[1].source.kind ==
        astraea::graphics::
            ShaderExportProbeValueSourceKind::
                vertex_index_position_y);
    REQUIRE(
        compiled->export_provenance.
            source_instruction.word_index == 0U);
    REQUIRE(
        compiled->export_provenance.
            source_instruction.word_count == 2U);
}

TEST_CASE(
    "owned MRT0 export becomes magenta compiler IR with provenance",
    "[graphics][compiler][export-probe][fragment]") {
    const auto program =
        owned_export_program(0x00U);
    const auto compiled =
        astraea::graphics::
            compile_shader_export_probe(
                program,
                astraea::graphics::
                    make_magenta_fragment_probe_launch_abi());

    REQUIRE(compiled.has_value());
    REQUIRE(
        compiled->stage ==
        astraea::graphics::
            ShaderExportProbeStage::fragment);
    REQUIRE(
        compiled->components[0].source.constant_bits ==
        0x3f800000U);
    REQUIRE(
        compiled->components[1].source.constant_bits ==
        0x00000000U);
    REQUIRE(
        compiled->components[2].source.constant_bits ==
        0x3f800000U);
    REQUIRE(
        compiled->components[3].source.constant_bits ==
        0x3f800000U);
    REQUIRE(
        compiled->export_provenance.
            source_instruction.word_count == 2U);
}

TEST_CASE(
    "export interpreter remains semantic oracle for synthetic probe inputs",
    "[graphics][compiler][export-probe][oracle]") {
    SECTION("Position0 lane") {
        const auto program =
            owned_export_program(0x0cU);

        astraea::graphics::ShaderScalarState scalar{};
        scalar.exec = 1U;
        astraea::graphics::ShaderVectorState vector{};
        vector.wave_size =
            astraea::graphics::ShaderWaveSize::wave32;
        vector.vgprs[0U][0U] = 0xbf800000U;
        vector.vgprs[1U][0U] = 0xbf800000U;
        vector.vgprs[2U][0U] = 0x00000000U;
        vector.vgprs[3U][0U] = 0x3f800000U;

        const auto effect =
            astraea::graphics::
                capture_shader_export_operation(
                    program.emissions[0].operation,
                    scalar,
                    vector);
        REQUIRE(effect.has_value());
        REQUIRE(
            effect->target_kind ==
            astraea::graphics::
                ShaderIrExportTargetKind::position);
        REQUIRE(effect->target_index == 0U);
        REQUIRE(
            effect->source_values[0U][0U] ==
            0xbf800000U);
        REQUIRE(
            effect->source_values[1U][0U] ==
            0xbf800000U);
        REQUIRE(
            effect->source_values[2U][0U] ==
            0x00000000U);
        REQUIRE(
            effect->source_values[3U][0U] ==
            0x3f800000U);
    }

    SECTION("MRT0 lane") {
        const auto program =
            owned_export_program(0x00U);

        astraea::graphics::ShaderScalarState scalar{};
        scalar.exec = 1U;
        astraea::graphics::ShaderVectorState vector{};
        vector.wave_size =
            astraea::graphics::ShaderWaveSize::wave32;
        vector.vgprs[0U][0U] = 0x3f800000U;
        vector.vgprs[1U][0U] = 0x00000000U;
        vector.vgprs[2U][0U] = 0x3f800000U;
        vector.vgprs[3U][0U] = 0x3f800000U;

        const auto effect =
            astraea::graphics::
                capture_shader_export_operation(
                    program.emissions[0].operation,
                    scalar,
                    vector);
        REQUIRE(effect.has_value());
        REQUIRE(
            effect->target_kind ==
            astraea::graphics::
                ShaderIrExportTargetKind::mrt);
        REQUIRE(effect->target_index == 0U);
        REQUIRE(
            effect->source_values[0U][0U] ==
            0x3f800000U);
        REQUIRE(
            effect->source_values[1U][0U] ==
            0x00000000U);
        REQUIRE(
            effect->source_values[2U][0U] ==
            0x3f800000U);
        REQUIRE(
            effect->source_values[3U][0U] ==
            0x3f800000U);
    }
}

TEST_CASE(
    "generated owned export modules validate for Vulkan 1.3",
    "[graphics][compiler][export-probe][spirv]") {
    const auto vertex_ir =
        astraea::graphics::
            compile_shader_export_probe(
                owned_export_program(0x0cU),
                astraea::graphics::
                    make_fullscreen_position_probe_launch_abi());
    const auto fragment_ir =
        astraea::graphics::
            compile_shader_export_probe(
                owned_export_program(0x00U),
                astraea::graphics::
                    make_magenta_fragment_probe_launch_abi());
    REQUIRE(vertex_ir.has_value());
    REQUIRE(fragment_ir.has_value());

    const auto vertex =
        astraea::graphics::
            lower_shader_export_probe_to_spirv(
                vertex_ir.value());
    const auto fragment =
        astraea::graphics::
            lower_shader_export_probe_to_spirv(
                fragment_ir.value());
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());

    const auto vertex_text =
        validate_and_disassemble(
            vertex->words);
    const auto fragment_text =
        validate_and_disassemble(
            fragment->words);

    REQUIRE(
        vertex_text.find("OpEntryPoint Vertex") !=
        std::string::npos);
    REQUIRE(
        vertex_text.find("BuiltIn VertexIndex") !=
        std::string::npos);
    REQUIRE(
        vertex_text.find("BuiltIn Position") !=
        std::string::npos);
    REQUIRE(
        vertex_text.find("OpShiftLeftLogical") !=
        std::string::npos);

    REQUIRE(
        fragment_text.find("OpEntryPoint Fragment") !=
        std::string::npos);
    REQUIRE(
        fragment_text.find("OriginUpperLeft") !=
        std::string::npos);
    REQUIRE(
        fragment_text.find("Location 0") !=
        std::string::npos);
}

TEST_CASE(
    "export probe compiler rejects unsupported semantic or launch assumptions",
    "[graphics][compiler][export-probe][negative]") {
    using Error =
        astraea::graphics::
            ShaderExportProbeCompilerErrorCode;

    SECTION("stage target mismatch") {
        const auto result =
            astraea::graphics::
                compile_shader_export_probe(
                    owned_export_program(0x0cU),
                    astraea::graphics::
                        make_magenta_fragment_probe_launch_abi());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_export_target);
    }

    SECTION("compressed export") {
        auto program =
            owned_export_program(0x0cU);
        auto& export_op =
            std::get<astraea::graphics::ShaderIrExport>(
                program.emissions[0].operation);
        export_op.compressed = true;

        const auto result =
            astraea::graphics::
                compile_shader_export_probe(
                    program,
                    astraea::graphics::
                        make_fullscreen_position_probe_launch_abi());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_export_form);
    }

    SECTION("duplicate launch binding") {
        auto launch =
            astraea::graphics::
                make_fullscreen_position_probe_launch_abi();
        launch.bindings[3].vgpr =
            launch.bindings[2].vgpr;

        const auto result =
            astraea::graphics::
                compile_shader_export_probe(
                    owned_export_program(0x0cU),
                    launch);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::duplicate_binding);
    }

    SECTION("missing launch binding") {
        auto launch =
            astraea::graphics::
                make_fullscreen_position_probe_launch_abi();
        launch.bindings[3].vgpr =
            astraea::graphics::ShaderIrVgpr{
                .index = 4U};

        const auto result =
            astraea::graphics::
                compile_shader_export_probe(
                    owned_export_program(0x0cU),
                    launch);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::missing_binding);
        REQUIRE(result.error().component_index == 3U);
        REQUIRE(result.error().vgpr_index == 3U);
    }

    SECTION("non-proof binding") {
        auto launch =
            astraea::graphics::
                make_magenta_fragment_probe_launch_abi();
        launch.bindings[1].source.constant_bits =
            0x3f800000U;

        const auto result =
            astraea::graphics::
                compile_shader_export_probe(
                    owned_export_program(0x00U),
                    launch);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::unsupported_probe_binding);
    }

    SECTION("extra semantic operation") {
        auto program =
            owned_export_program(0x0cU);
        program.emissions.push_back(
            program.emissions.back());

        const auto result =
            astraea::graphics::
                compile_shader_export_probe(
                    program,
                    astraea::graphics::
                        make_fullscreen_position_probe_launch_abi());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            Error::invalid_program_shape);
    }
}
