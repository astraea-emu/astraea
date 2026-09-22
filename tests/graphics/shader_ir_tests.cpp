#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_ir.hpp>

#include <array>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;
constexpr std::uint32_t kSop1Base = 0xbe800000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
}

astraea::graphics::Rdna2Instruction decode_one(
    std::uint32_t word) {
    const std::array<std::uint32_t, 1> words{word};
    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

constexpr std::uint32_t make_sop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint8_t source) {
    return kSop1Base |
           (static_cast<std::uint32_t>(destination) << 16U) |
           (static_cast<std::uint32_t>(opcode) << 8U) |
           static_cast<std::uint32_t>(source);
}

}  // namespace

TEST_CASE(
    "S_NOP lowers to semantic repeat count and preserves provenance",
    "[graphics][shader-ir]") {
    auto instruction =
        decode_one(make_sopp(0, 0x000f));
    const auto expected_instruction = instruction;

    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            std::move(instruction));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrNop>(
            emission.operation));
    REQUIRE(
        std::get<astraea::graphics::ShaderIrNop>(
            emission.operation)
            .repeat_count == 16);
    REQUIRE(
        emission.provenance.source_instruction ==
        expected_instruction);
}

TEST_CASE(
    "S_ENDPGM lowers to typed end-program operation",
    "[graphics][shader-ir]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(1, 0)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            emission.operation));
}

TEST_CASE(
    "S_BRANCH lowers to signed byte delta from current instruction",
    "[graphics][shader-ir]") {
    SECTION("forward") {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(2, 4)));

        REQUIRE(
            std::get<
                astraea::graphics::ShaderIrRelativeBranch>(
                emission.operation)
                .byte_delta == 20);
    }

    SECTION("backward") {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(2, 0xfffe)));

        REQUIRE(
            std::get<
                astraea::graphics::ShaderIrRelativeBranch>(
                emission.operation)
                .byte_delta == -4);
    }
}

TEST_CASE(
    "unknown SOPP opcode remains an explicit unsupported Shader IR operation",
    "[graphics][shader-ir]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(0x7e, 0x1234)));

    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrUnsupported>(
            emission.operation)
            .reason ==
        astraea::graphics::ShaderIrUnsupportedReason::
            unknown_sopp_opcode);
}

TEST_CASE(
    "unsupported RDNA2 encoding remains explicit in Shader IR",
    "[graphics][shader-ir]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(0x01234567U));

    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrUnsupported>(
            emission.operation)
            .reason ==
        astraea::graphics::ShaderIrUnsupportedReason::
            unsupported_encoding);
}

TEST_CASE(
    "inconsistent decoded instruction is not silently interpreted",
    "[graphics][shader-ir]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            astraea::graphics::Rdna2Instruction{
                .word_index = 0,
                .byte_offset = 0,
                .raw_word = 0,
                .raw_encoding = {},
                .format =
                    astraea::graphics::
                        Rdna2InstructionFormat::unsupported,
                .kind =
                    astraea::graphics::
                        Rdna2InstructionKind::s_branch,
                .sopp = std::nullopt,
            });

    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrUnsupported>(
            emission.operation)
            .reason ==
        astraea::graphics::ShaderIrUnsupportedReason::
            invalid_decoded_instruction);
}

TEST_CASE(
    "Shader IR semantic equality excludes raw instruction provenance",
    "[graphics][shader-ir]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(0, 0x0000)));
    const auto right =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(0, 0x0010)));

    REQUIRE(
        left.provenance.source_instruction !=
        right.provenance.source_instruction);
    REQUIRE(
        std::get<astraea::graphics::ShaderIrNop>(
            left.operation)
            .repeat_count == 1);
    REQUIRE(
        std::get<astraea::graphics::ShaderIrNop>(
            right.operation)
            .repeat_count == 1);
    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            right));
}


TEST_CASE(
    "RDNA2 conditional branches lower to typed condition and signed byte delta",
    "[graphics][shader-ir][conditional-branch]") {
    struct Case {
        std::uint8_t opcode;
        astraea::graphics::ShaderIrBranchCondition condition;
    };

    constexpr std::array<Case, 6> cases{{
        {4, astraea::graphics::ShaderIrBranchCondition::scc_zero},
        {5, astraea::graphics::ShaderIrBranchCondition::scc_one},
        {6, astraea::graphics::ShaderIrBranchCondition::vcc_zero},
        {7, astraea::graphics::ShaderIrBranchCondition::vcc_nonzero},
        {8, astraea::graphics::ShaderIrBranchCondition::exec_zero},
        {9, astraea::graphics::ShaderIrBranchCondition::exec_nonzero},
    }};

    for (const auto& test_case : cases) {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(
                    make_sopp(test_case.opcode, 4)));

        REQUIRE(
            std::holds_alternative<
                astraea::graphics::
                    ShaderIrConditionalRelativeBranch>(
                emission.operation));
        const auto& branch =
            std::get<
                astraea::graphics::
                    ShaderIrConditionalRelativeBranch>(
                emission.operation);
        REQUIRE(branch.condition == test_case.condition);
        REQUIRE(branch.byte_delta == 20);
        REQUIRE(
            emission.provenance.source_instruction.sopp
                .has_value());
        REQUIRE(
            emission.provenance.source_instruction.sopp->opcode ==
            test_case.opcode);
    }
}

TEST_CASE(
    "conditional branch lowering sign-extends backward displacement",
    "[graphics][shader-ir][conditional-branch]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(8, 0xfffe)));

    const auto& branch =
        std::get<
            astraea::graphics::
                ShaderIrConditionalRelativeBranch>(
            emission.operation);
    REQUIRE(
        branch.condition ==
        astraea::graphics::ShaderIrBranchCondition::
            exec_zero);
    REQUIRE(branch.byte_delta == -4);
}


TEST_CASE(
    "RDNA2 S_BARRIER lowers to workgroup barrier Shader IR",
    "[graphics][shader-ir][barrier]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(10, 0x1234)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrWorkgroupBarrier>(
            emission.operation));
    REQUIRE(
        emission.provenance.source_instruction.kind ==
        astraea::graphics::Rdna2InstructionKind::s_barrier);
    REQUIRE(
        emission.provenance.source_instruction.sopp
            .has_value());
    REQUIRE(
        emission.provenance.source_instruction.sopp->opcode ==
        10);
}

TEST_CASE(
    "S_BARRIER Shader IR semantics exclude unused immediate provenance",
    "[graphics][shader-ir][barrier]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(10, 0x0000)));
    const auto right =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(10, 0x7fff)));

    REQUIRE(
        left.provenance.source_instruction !=
        right.provenance.source_instruction);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrWorkgroupBarrier>(
            left.operation));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrWorkgroupBarrier>(
            right.operation));
    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            right));
}


TEST_CASE(
    "RDNA2 S_WAITCNT lowers documented counter thresholds",
    "[graphics][shader-ir][waitcnt]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(12, 0xaa35)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrWaitCount>(
            emission.operation));

    const auto& wait =
        std::get<
            astraea::graphics::ShaderIrWaitCount>(
            emission.operation);
    REQUIRE(wait.vmcnt == 37);
    REQUIRE(wait.expcnt == 3);
    REQUIRE(wait.lgkmcnt == 42);
    REQUIRE(
        emission.provenance.source_instruction.kind ==
        astraea::graphics::Rdna2InstructionKind::s_waitcnt);
}

TEST_CASE(
    "S_WAITCNT semantic equality excludes reserved SIMM16 bit 7",
    "[graphics][shader-ir][waitcnt]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(12, 0xaa35)));
    const auto right =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(12, 0xaab5)));

    REQUIRE(
        left.provenance.source_instruction !=
        right.provenance.source_instruction);
    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            right));
}


TEST_CASE(
    "RDNA2 S_MOV_B32 lowers plain SGPR move",
    "[graphics][shader-ir][sop1][mov]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 17)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrScalarMove32>(
            emission.operation));
    const auto& move =
        std::get<
            astraea::graphics::ShaderIrScalarMove32>(
            emission.operation);
    REQUIRE(move.destination.index == 5);
    REQUIRE(move.source.index == 17);
}

TEST_CASE(
    "S_MOV_B32 non-SGPR operands remain typed unsupported",
    "[graphics][shader-ir][sop1][mov]") {
    constexpr std::array<std::uint8_t, 3> sources{
        106,
        128,
        255,
    };

    for (const auto source : sources) {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, source)));

        REQUIRE(
            std::holds_alternative<
                astraea::graphics::ShaderIrUnsupported>(
                emission.operation));
        REQUIRE(
            std::get<
                astraea::graphics::ShaderIrUnsupported>(
                emission.operation)
                .reason ==
            astraea::graphics::
                ShaderIrUnsupportedReason::
                    unsupported_scalar_operand);
    }

    const auto special_destination =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 106, 5)));
    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrUnsupported>(
            special_destination.operation)
            .reason ==
        astraea::graphics::
            ShaderIrUnsupportedReason::
                unsupported_scalar_operand);
}

TEST_CASE(
    "S_MOV_B32 SGPR identity participates in semantic equality",
    "[graphics][shader-ir][sop1][mov]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 17)));
    const auto same =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 17)));
    const auto different =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 6, 17)));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different));
}
