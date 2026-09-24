#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_program.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kExpBase = 0xf8000000U;
constexpr std::uint32_t kSoppEndPgm = 0xbf810000U;

constexpr std::uint32_t make_exp_word0(
    std::uint8_t enable_mask,
    std::uint8_t target,
    bool compressed = false,
    bool done = false,
    bool valid_mask = false) {
    return kExpBase |
           (static_cast<std::uint32_t>(enable_mask) & 0x0fU) |
           ((static_cast<std::uint32_t>(target) & 0x3fU) << 4U) |
           (compressed ? (1U << 10U) : 0U) |
           (done ? (1U << 11U) : 0U) |
           (valid_mask ? (1U << 12U) : 0U);
}

constexpr std::uint32_t make_exp_sources(
    std::uint8_t source0,
    std::uint8_t source1,
    std::uint8_t source2,
    std::uint8_t source3) {
    return
        static_cast<std::uint32_t>(source0) |
        (static_cast<std::uint32_t>(source1) << 8U) |
        (static_cast<std::uint32_t>(source2) << 16U) |
        (static_cast<std::uint32_t>(source3) << 24U);
}

astraea::graphics::Rdna2Instruction decode_exp(
    std::uint8_t enable_mask,
    std::uint8_t target,
    bool compressed,
    bool done,
    bool valid_mask,
    std::array<std::uint8_t, 4> sources) {
    const std::array<std::uint32_t, 2> words{
        make_exp_word0(
            enable_mask,
            target,
            compressed,
            done,
            valid_mask),
        make_exp_sources(
            sources[0],
            sources[1],
            sources[2],
            sources[3]),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0U);
    REQUIRE(result.has_value());
    return result.value();
}

astraea::graphics::ShaderIrEmission lower_exp(
    std::uint8_t enable_mask,
    std::uint8_t target,
    bool compressed = false,
    bool done = false,
    bool valid_mask = false,
    std::array<std::uint8_t, 4> sources =
        {0U, 1U, 2U, 3U}) {
    return astraea::graphics::lower_rdna2_to_shader_ir(
        decode_exp(
            enable_mask,
            target,
            compressed,
            done,
            valid_mask,
            sources));
}

}  // namespace

TEST_CASE(
    "RDNA2 EXP decodes the exact two-dword field layout",
    "[graphics][rdna2][exp][decode]") {
    const auto instruction =
        decode_exp(
            0x0dU,
            0x2aU,
            true,
            true,
            true,
            {4U, 17U, 128U, 255U});

    REQUIRE(
        instruction.format ==
        astraea::graphics::Rdna2InstructionFormat::exp);
    REQUIRE(
        instruction.kind ==
        astraea::graphics::Rdna2InstructionKind::exp);
    REQUIRE(instruction.word_count == 2U);
    REQUIRE(instruction.word_index == 0U);
    REQUIRE(instruction.byte_offset == 0U);
    REQUIRE(instruction.exp.has_value());
    REQUIRE(instruction.exp->enable_mask == 0x0dU);
    REQUIRE(instruction.exp->target == 0x2aU);
    REQUIRE(instruction.exp->compressed);
    REQUIRE(instruction.exp->done);
    REQUIRE(instruction.exp->valid_mask);
    REQUIRE(
        instruction.exp->source_vgprs ==
        std::array<std::uint8_t, 4>{
            4U, 17U, 128U, 255U});
}

TEST_CASE(
    "RDNA2 EXP rejects a truncated second dword",
    "[graphics][rdna2][exp][decode][negative]") {
    const std::array<std::uint32_t, 1> words{
        make_exp_word0(
            0x0fU,
            0U,
            false,
            true,
            false),
    };

    const auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0U);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::Rdna2DecodeErrorCode::
            instruction_out_of_bounds);
    REQUIRE(result.error().word_index == 1U);
    REQUIRE(result.error().available_words == 1U);
}

TEST_CASE(
    "Shader IR classifies generic EXP target ranges",
    "[graphics][shader-ir][exp][target]") {
    using Kind =
        astraea::graphics::ShaderIrExportTargetKind;

    struct Case {
        std::uint8_t target;
        Kind kind;
        std::uint8_t index;
    };

    constexpr std::array<Case, 10> cases{{
        {0x00U, Kind::mrt, 0U},
        {0x07U, Kind::mrt, 7U},
        {0x08U, Kind::mrt_z, 0U},
        {0x09U, Kind::null_target, 0U},
        {0x0cU, Kind::position, 0U},
        {0x0fU, Kind::position, 3U},
        {0x14U, Kind::primitive, 0U},
        {0x20U, Kind::parameter, 0U},
        {0x21U, Kind::parameter, 1U},
        {0x3fU, Kind::parameter, 31U},
    }};

    for (const auto& test_case : cases) {
        const auto emission =
            lower_exp(
                0x0fU,
                test_case.target);

        REQUIRE(
            std::holds_alternative<
                astraea::graphics::ShaderIrExport>(
                emission.operation));
        const auto& export_op =
            std::get<
                astraea::graphics::ShaderIrExport>(
                emission.operation);
        REQUIRE(export_op.target_kind == test_case.kind);
        REQUIRE(export_op.target_index == test_case.index);
    }
}

TEST_CASE(
    "Shader IR EXP preserves mask flags and VGPR identities",
    "[graphics][shader-ir][exp]") {
    const auto emission =
        lower_exp(
            0x0bU,
            0x0cU,
            true,
            true,
            true,
            {7U, 8U, 9U, 10U});

    const auto& export_op =
        std::get<
            astraea::graphics::ShaderIrExport>(
            emission.operation);

    REQUIRE(export_op.enable_mask == 0x0bU);
    REQUIRE(export_op.compressed);
    REQUIRE(export_op.done);
    REQUIRE(export_op.valid_mask);
    REQUIRE(export_op.sources[0].index == 7U);
    REQUIRE(export_op.sources[1].index == 8U);
    REQUIRE(export_op.sources[2].index == 9U);
    REQUIRE(export_op.sources[3].index == 10U);
}

TEST_CASE(
    "unknown EXP target remains explicit unsupported Shader IR",
    "[graphics][shader-ir][exp][negative]") {
    const auto emission =
        lower_exp(
            0x0fU,
            0x0aU);

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrUnsupported>(
            emission.operation));
    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrUnsupported>(
            emission.operation)
            .reason ==
        astraea::graphics::ShaderIrUnsupportedReason::
            unknown_export_target);
}

TEST_CASE(
    "EXP semantic equality includes target flags mask and sources",
    "[graphics][shader-ir][exp][equality]") {
    const auto base =
        lower_exp(
            0x0fU,
            0U,
            false,
            true,
            false,
            {1U, 2U, 3U, 4U});
    const auto same =
        lower_exp(
            0x0fU,
            0U,
            false,
            true,
            false,
            {1U, 2U, 3U, 4U});
    const auto different_target =
        lower_exp(
            0x0fU,
            0x0cU,
            false,
            true,
            false,
            {1U, 2U, 3U, 4U});
    const auto different_done =
        lower_exp(
            0x0fU,
            0U,
            false,
            false,
            false,
            {1U, 2U, 3U, 4U});
    const auto different_source =
        lower_exp(
            0x0fU,
            0U,
            false,
            true,
            false,
            {1U, 2U, 3U, 5U});

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            base,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            base,
            different_target));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            base,
            different_done));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            base,
            different_source));
}

TEST_CASE(
    "shader program stream advancement consumes both EXP dwords",
    "[graphics][shader-program][exp]") {
    const std::array<std::uint32_t, 3> words{
        make_exp_word0(
            0x0fU,
            0U,
            false,
            true,
            false),
        make_exp_sources(
            0U,
            1U,
            2U,
            3U),
        kSoppEndPgm,
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);

    REQUIRE(program.has_value());
    REQUIRE(program->source_word_count == 3U);
    REQUIRE(program->emissions.size() == 2U);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrExport>(
            program->emissions[0].operation));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            program->emissions[1].operation));
    REQUIRE(
        program->emissions[0].
            provenance.source_instruction.word_count ==
        2U);
    REQUIRE(
        program->emissions[1].
            provenance.source_instruction.word_index ==
        2U);
}
