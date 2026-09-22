#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_ir.hpp>

#include <array>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;
constexpr std::uint32_t kSop1Base = 0xbe800000U;
constexpr std::uint32_t kVop1Base = 0x7e000000U;

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

astraea::graphics::Rdna2Instruction decode_literal(
    std::uint32_t word,
    std::uint32_t literal) {
    const std::array<std::uint32_t, 2> words{
        word,
        literal,
    };
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

constexpr std::uint32_t make_vop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source) {
    return kVop1Base |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(opcode) << 9U) |
           static_cast<std::uint32_t>(source);
}

constexpr std::uint32_t make_vop2(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source0,
    std::uint8_t source1) {
    return (static_cast<std::uint32_t>(opcode) << 25U) |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(source1) << 9U) |
           static_cast<std::uint32_t>(source0);
}

astraea::graphics::Rdna2Instruction decode_vop1_extension(
    std::uint32_t word,
    std::uint32_t extension) {
    const std::array<std::uint32_t, 2> words{
        word,
        extension,
    };
    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::graphics::Rdna2Instruction decode_vop2_extension(
    std::uint32_t word,
    std::uint32_t extension) {
    const std::array<std::uint32_t, 2> words{
        word,
        extension,
    };
    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
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
            decode_one(0x80000000U));

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
                .sop1 = std::nullopt,
                .vop1 = std::nullopt,
                .vop2 = std::nullopt,
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
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrSgpr>(
            move.source));
    REQUIRE(
        std::get<astraea::graphics::ShaderIrSgpr>(
            move.source)
            .index == 17);
}

TEST_CASE(
    "S_MOV_B32 documented inline integer sources lower exactly",
    "[graphics][shader-ir][sop1][mov][inline-integer]") {
    struct Case {
        std::uint8_t selector;
        std::int32_t value;
    };

    constexpr std::array<Case, 5> cases{{
        {128, 0},
        {129, 1},
        {192, 64},
        {193, -1},
        {208, -16},
    }};

    for (const auto& test_case : cases) {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(
                    make_sop1(
                        3,
                        5,
                        test_case.selector)));

        REQUIRE(
            std::holds_alternative<
                astraea::graphics::
                    ShaderIrScalarMove32>(
                emission.operation));
        const auto& move =
            std::get<
                astraea::graphics::
                    ShaderIrScalarMove32>(
                emission.operation);
        REQUIRE(move.destination.index == 5);
        REQUIRE(
            std::holds_alternative<
                astraea::graphics::
                    ShaderIrInlineInteger32>(
                move.source));
        REQUIRE(
            std::get<
                astraea::graphics::
                    ShaderIrInlineInteger32>(
                move.source)
                .value == test_case.value);
    }
}

TEST_CASE(
    "S_MOV_B32 unsupported scalar selectors remain typed unsupported",
    "[graphics][shader-ir][sop1][mov]") {
    constexpr std::array<std::uint8_t, 6> sources{
        108,
        123,
        209,
        250,
        251,
        254,
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

TEST_CASE(
    "S_MOV_B32 literal source lowers as exact 32-bit bits",
    "[graphics][shader-ir][sop1][mov][literal]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_literal(
                make_sop1(3, 5, 255),
                0xdeadbeefU));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrScalarMove32>(
            emission.operation));
    const auto& move =
        std::get<
            astraea::graphics::ShaderIrScalarMove32>(
            emission.operation);
    REQUIRE(move.destination.index == 5);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrLiteral32>(
            move.source));
    REQUIRE(
        std::get<astraea::graphics::ShaderIrLiteral32>(
            move.source)
            .bits == 0xdeadbeefU);
    REQUIRE(
        emission.provenance.source_instruction.word_count ==
        2);
}

TEST_CASE(
    "S_MOV_B32 literal bits participate in semantic equality",
    "[graphics][shader-ir][sop1][mov][literal]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_literal(
                make_sop1(3, 5, 255),
                0x12345678U));
    const auto same =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_literal(
                make_sop1(3, 5, 255),
                0x12345678U));
    const auto different =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_literal(
                make_sop1(3, 5, 255),
                0x12345679U));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different));
}

TEST_CASE(
    "S_MOV_B32 inline integer value participates in semantic equality",
    "[graphics][shader-ir][sop1][mov][inline-integer]") {
    const auto zero =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 128)));
    const auto same_zero =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 128)));
    const auto one =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 129)));
    const auto sgpr =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 0)));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            zero,
            same_zero));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            zero,
            one));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            zero,
            sgpr));
}


TEST_CASE(
    "RDNA2 S_MOV_B64 lowers even SGPR pairs",
    "[graphics][shader-ir][sop1][mov64]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(4, 4, 16)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrScalarMove64>(
            emission.operation));
    const auto& move =
        std::get<
            astraea::graphics::ShaderIrScalarMove64>(
            emission.operation);
    REQUIRE(move.destination.first_index == 4);
    REQUIRE(move.source.first_index == 16);
}

TEST_CASE(
    "S_MOV_B64 invalid SGPR pairs remain typed unsupported",
    "[graphics][shader-ir][sop1][mov64]") {
    struct Case {
        std::uint8_t destination;
        std::uint8_t source;
    };

    constexpr std::array<Case, 6> cases{{
        {5, 16},
        {4, 17},
        {105, 16},
        {4, 105},
        {4, 106},
        {4, 255},
    }};

    for (const auto& test_case : cases) {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(
                    make_sop1(
                        4,
                        test_case.destination,
                        test_case.source)));

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
}

TEST_CASE(
    "S_MOV_B64 SGPR-pair identity participates in semantic equality",
    "[graphics][shader-ir][sop1][mov64]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(4, 4, 16)));
    const auto same =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(4, 4, 16)));
    const auto different =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(4, 6, 16)));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different));
}


TEST_CASE(
    "S_MOV_B32 documented special scalar sources lower exactly",
    "[graphics][shader-ir][sop1][mov][special-source]") {
    using Kind =
        astraea::graphics::ShaderIrSpecialScalarSourceKind32;

    struct Case {
        std::uint8_t selector;
        Kind kind;
    };

    constexpr std::array<Case, 6> cases{{
        {106, Kind::vcc_lo},
        {107, Kind::vcc_hi},
        {124, Kind::m0},
        {125, Kind::null_register},
        {126, Kind::exec_lo},
        {127, Kind::exec_hi},
    }};

    for (const auto& test_case : cases) {
        const auto emission =
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(
                    make_sop1(
                        3,
                        5,
                        test_case.selector)));

        REQUIRE(
            std::holds_alternative<
                astraea::graphics::ShaderIrScalarMove32>(
                emission.operation));

        const auto& move =
            std::get<
                astraea::graphics::ShaderIrScalarMove32>(
                emission.operation);
        REQUIRE(move.destination.index == 5);
        REQUIRE(
            std::holds_alternative<
                astraea::graphics::
                    ShaderIrSpecialScalarSource32>(
                move.source));
        REQUIRE(
            std::get<
                astraea::graphics::
                    ShaderIrSpecialScalarSource32>(
                move.source)
                .kind == test_case.kind);
    }
}

TEST_CASE(
    "S_MOV_B32 special scalar source identity participates in semantic equality",
    "[graphics][shader-ir][sop1][mov][special-source]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 106)));
    const auto same =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 106)));
    const auto different =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 107)));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different));
}


TEST_CASE(
    "RDNA2 V_MOV_B32 lowers plain VGPR move",
    "[graphics][shader-ir][vop1][mov]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 5, 273)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrVectorMove32>(
            emission.operation));
    const auto& move =
        std::get<
            astraea::graphics::ShaderIrVectorMove32>(
            emission.operation);
    REQUIRE(move.destination.index == 5);
    REQUIRE(move.source.index == 17);
}

TEST_CASE(
    "V_MOV_B32 VGPR source boundaries lower exactly",
    "[graphics][shader-ir][vop1][mov]") {
    const auto first =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 0, 256)));
    const auto last =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 255, 511)));

    REQUIRE(
        std::get<astraea::graphics::ShaderIrVectorMove32>(
            first.operation)
            .source.index == 0);
    REQUIRE(
        std::get<astraea::graphics::ShaderIrVectorMove32>(
            last.operation)
            .source.index == 255);
    REQUIRE(
        std::get<astraea::graphics::ShaderIrVectorMove32>(
            last.operation)
            .destination.index == 255);
}

TEST_CASE(
    "V_MOV_B32 non-VGPR source forms remain typed unsupported",
    "[graphics][shader-ir][vop1][mov]") {
    const auto scalar =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 5, 17)));
    const auto dpp =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_vop1_extension(
                make_vop1(1, 5, 250),
                0x12345678U));
    const auto literal =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_vop1_extension(
                make_vop1(1, 5, 255),
                0xdeadbeefU));

    for (const auto* emission :
         std::array{
             &scalar,
             &dpp,
             &literal,
         }) {
        REQUIRE(
            std::holds_alternative<
                astraea::graphics::ShaderIrUnsupported>(
                emission->operation));
        REQUIRE(
            std::get<
                astraea::graphics::ShaderIrUnsupported>(
                emission->operation)
                .reason ==
            astraea::graphics::ShaderIrUnsupportedReason::
                unsupported_vector_operand);
    }
}

TEST_CASE(
    "V_MOV_B32 VGPR identities participate in semantic equality",
    "[graphics][shader-ir][vop1][mov]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 5, 273)));
    const auto same =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 5, 273)));
    const auto different =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 6, 273)));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different));
}


TEST_CASE(
    "unknown VOP2 opcode remains an explicit unsupported Shader IR operation",
    "[graphics][shader-ir][vop2]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(4, 5, 273, 9)));

    REQUIRE(
        std::get<
            astraea::graphics::ShaderIrUnsupported>(
            emission.operation)
            .reason ==
        astraea::graphics::ShaderIrUnsupportedReason::
            unknown_vop2_opcode);
}

TEST_CASE(
    "RDNA2 V_ADD_F32 lowers plain VGPR arithmetic",
    "[graphics][shader-ir][vop2][add-f32]") {
    const auto emission =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 273, 9)));

    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrVectorAddF32>(
            emission.operation));
    const auto& add =
        std::get<
            astraea::graphics::ShaderIrVectorAddF32>(
            emission.operation);
    REQUIRE(add.destination.index == 5);
    REQUIRE(add.source0.index == 17);
    REQUIRE(add.source1.index == 9);
}

TEST_CASE(
    "V_ADD_F32 VGPR SRC0 boundaries lower exactly",
    "[graphics][shader-ir][vop2][add-f32]") {
    const auto first =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 0, 256, 0)));
    const auto last =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 255, 511, 255)));

    const auto& first_add =
        std::get<
            astraea::graphics::ShaderIrVectorAddF32>(
            first.operation);
    const auto& last_add =
        std::get<
            astraea::graphics::ShaderIrVectorAddF32>(
            last.operation);

    REQUIRE(first_add.destination.index == 0);
    REQUIRE(first_add.source0.index == 0);
    REQUIRE(first_add.source1.index == 0);
    REQUIRE(last_add.destination.index == 255);
    REQUIRE(last_add.source0.index == 255);
    REQUIRE(last_add.source1.index == 255);
}

TEST_CASE(
    "V_ADD_F32 non-VGPR SRC0 forms remain typed unsupported",
    "[graphics][shader-ir][vop2][add-f32]") {
    const auto scalar =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 17, 9)));
    const auto dpp =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_vop2_extension(
                make_vop2(3, 5, 250, 9),
                0x12345678U));
    const auto literal =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_vop2_extension(
                make_vop2(3, 5, 255, 9),
                0xdeadbeefU));

    for (const auto* emission :
         std::array{
             &scalar,
             &dpp,
             &literal,
         }) {
        REQUIRE(
            std::holds_alternative<
                astraea::graphics::ShaderIrUnsupported>(
                emission->operation));
        REQUIRE(
            std::get<
                astraea::graphics::ShaderIrUnsupported>(
                emission->operation)
                .reason ==
            astraea::graphics::ShaderIrUnsupportedReason::
                unsupported_vector_operand);
    }
}

TEST_CASE(
    "V_ADD_F32 VGPR identities participate in semantic equality",
    "[graphics][shader-ir][vop2][add-f32]") {
    const auto left =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 273, 9)));
    const auto same =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 273, 9)));
    const auto different_destination =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 6, 273, 9)));
    const auto different_source0 =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 274, 9)));
    const auto different_source1 =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 273, 10)));

    REQUIRE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            same));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different_destination));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different_source0));
    REQUIRE_FALSE(
        astraea::graphics::shader_ir_semantically_equal(
            left,
            different_source1));
}
