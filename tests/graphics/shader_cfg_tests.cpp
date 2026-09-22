#include <astraea/graphics/shader_cfg.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

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
    "empty Shader IR program builds an empty CFG",
    "[graphics][shader-cfg]") {
    const astraea::graphics::ShaderIrProgram program{};

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(program);

    REQUIRE(result.has_value());
    REQUIRE(result->source_word_count == 0);
    REQUIRE(result->emission_count == 0);
    REQUIRE(result->blocks.empty());
}

TEST_CASE(
    "unconditional branch splits forward target and unreachable source region",
    "[graphics][shader-cfg][branch]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(0, 0),
        make_sopp(2, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());

    REQUIRE(result.has_value());
    REQUIRE(result->blocks.size() == 3);

    const auto& entry = result->blocks[0];
    REQUIRE(entry.first_emission_index == 0);
    REQUIRE(entry.emission_count == 2);
    REQUIRE(entry.successors.size() == 1);
    REQUIRE(
        entry.successors[0].kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            unconditional_branch);
    REQUIRE(
        entry.successors[0].target_block_index == 2);

    const auto& unreachable = result->blocks[1];
    REQUIRE(unreachable.first_emission_index == 2);
    REQUIRE(unreachable.emission_count == 1);
    REQUIRE(unreachable.successors.size() == 1);
    REQUIRE(
        unreachable.successors[0].kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            linear_fallthrough);
    REQUIRE(
        unreachable.successors[0].target_block_index == 2);

    const auto& target = result->blocks[2];
    REQUIRE(target.first_emission_index == 3);
    REQUIRE(target.emission_count == 1);
    REQUIRE(target.successors.empty());
}

TEST_CASE(
    "conditional branch emits taken and fallthrough edges",
    "[graphics][shader-cfg][branch][conditional]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(4, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());

    REQUIRE(result.has_value());
    REQUIRE(result->blocks.size() == 3);

    const auto& branch = result->blocks[0];
    REQUIRE(branch.emission_count == 1);
    REQUIRE(branch.successors.size() == 2);
    REQUIRE(
        branch.successors[0].kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            conditional_branch_taken);
    REQUIRE(
        branch.successors[0].target_block_index == 2);
    REQUIRE(
        branch.successors[1].kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            conditional_branch_fallthrough);
    REQUIRE(
        branch.successors[1].target_block_index == 1);

    const auto& false_path = result->blocks[1];
    REQUIRE(false_path.successors.size() == 1);
    REQUIRE(
        false_path.successors[0].kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            linear_fallthrough);
    REQUIRE(
        false_path.successors[0].target_block_index == 2);
}

TEST_CASE(
    "backward unconditional branch can form a self-loop block",
    "[graphics][shader-cfg][branch][loop]") {
    const std::array<std::uint32_t, 2> words{
        make_sopp(0, 0),
        make_sopp(2, 0xfffeU),
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());

    REQUIRE(result.has_value());
    REQUIRE(result->blocks.size() == 1);
    REQUIRE(result->blocks[0].emission_count == 2);
    REQUIRE(result->blocks[0].successors.size() == 1);
    REQUIRE(
        result->blocks[0].successors[0].kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            unconditional_branch);
    REQUIRE(
        result->blocks[0]
            .successors[0]
            .target_block_index == 0);
}

TEST_CASE(
    "S_ENDPGM terminates a block without falling into later source code",
    "[graphics][shader-cfg][terminal]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(1, 0),
        make_sopp(0, 0),
        make_sopp(1, 0),
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());

    REQUIRE(result.has_value());
    REQUIRE(result->blocks.size() == 2);
    REQUIRE(result->blocks[0].emission_count == 1);
    REQUIRE(result->blocks[0].successors.empty());
    REQUIRE(result->blocks[1].first_emission_index == 1);
    REQUIRE(result->blocks[1].emission_count == 2);
    REQUIRE(result->blocks[1].successors.empty());
}

TEST_CASE(
    "branch into literal extension dword is rejected",
    "[graphics][shader-cfg][branch][extension]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 5, 255),
        0xdeadbeefU,
        make_sopp(2, 0xfffeU),
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    REQUIRE(program->emissions.size() == 2);
    REQUIRE(
        program->emissions[0]
            .provenance.source_instruction.word_count ==
        2);

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::ShaderCfgErrorCode::
            invalid_branch_target);
    REQUIRE(result.error().emission_index == 1);
    REQUIRE(result.error().source_word_index == 2);
    REQUIRE(
        result.error().target_word_index ==
        std::optional<std::int64_t>{1});
}

TEST_CASE(
    "negative out-of-bounds branch target is rejected",
    "[graphics][shader-cfg][branch][bounds]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(2, 0xfffeU),
    };

    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::ShaderCfgErrorCode::
            invalid_branch_target);
    REQUIRE(
        result.error().target_word_index ==
        std::optional<std::int64_t>{-1});
}

TEST_CASE(
    "non-dword branch delta is rejected instead of rounded",
    "[graphics][shader-cfg][branch][validation]") {
    const std::array<std::uint32_t, 2> words{
        make_sopp(2, 0),
        make_sopp(1, 0),
    };

    auto lowered =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(lowered.has_value());

    auto program = std::move(lowered).value();
    auto& branch =
        std::get<
            astraea::graphics::ShaderIrRelativeBranch>(
            program.emissions[0].operation);
    branch.byte_delta = 2;

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(program);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::ShaderCfgErrorCode::
            invalid_branch_delta);
    REQUIRE_FALSE(
        result.error().target_word_index.has_value());
}

TEST_CASE(
    "CFG rejects noncontiguous Shader IR program provenance",
    "[graphics][shader-cfg][validation]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(1, 0),
    };

    auto lowered =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(lowered.has_value());

    auto program = std::move(lowered).value();
    program.emissions[0]
        .provenance.source_instruction.byte_offset = 4;

    const auto result =
        astraea::graphics::
            build_shader_control_flow_graph(program);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::ShaderCfgErrorCode::
            invalid_program_layout);
    REQUIRE(result.error().emission_index == 0);
}
