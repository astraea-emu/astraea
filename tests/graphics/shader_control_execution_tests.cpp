#include <astraea/graphics/shader_control_execution.hpp>

#include <array>
#include <cstdint>
#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
}

}  // namespace

TEST_CASE(
    "SCC branch predicates use explicit scalar condition state",
    "[graphics][shader-execution][branch][scc]") {
    astraea::graphics::ShaderScalarState state{};

    SECTION("SCC zero") {
        state.scc = false;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::scc_zero,
                    state);
        const auto one =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::scc_one,
                    state);

        REQUIRE(zero.taken);
        REQUIRE_FALSE(one.taken);
    }

    SECTION("SCC one") {
        state.scc = true;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::scc_zero,
                    state);
        const auto one =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::scc_one,
                    state);

        REQUIRE_FALSE(zero.taken);
        REQUIRE(one.taken);
    }
}

TEST_CASE(
    "VCC branch predicates test the full explicit mask",
    "[graphics][shader-execution][branch][vcc]") {
    astraea::graphics::ShaderScalarState state{};

    SECTION("VCC zero") {
        state.vcc = 0;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::vcc_zero,
                    state);
        const auto nonzero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::vcc_nonzero,
                    state);

        REQUIRE(zero.taken);
        REQUIRE_FALSE(nonzero.taken);
    }

    SECTION("VCC low dword nonzero") {
        state.vcc = 1;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::vcc_zero,
                    state);
        const auto nonzero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::vcc_nonzero,
                    state);

        REQUIRE_FALSE(zero.taken);
        REQUIRE(nonzero.taken);
    }

    SECTION("VCC high dword nonzero") {
        state.vcc = 0x8000000000000000ULL;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::vcc_zero,
                    state);
        const auto nonzero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::vcc_nonzero,
                    state);

        REQUIRE_FALSE(zero.taken);
        REQUIRE(nonzero.taken);
    }
}

TEST_CASE(
    "EXEC branch predicates test the full explicit mask",
    "[graphics][shader-execution][branch][exec]") {
    astraea::graphics::ShaderScalarState state{};

    SECTION("EXEC zero") {
        state.exec = 0;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::exec_zero,
                    state);
        const auto nonzero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::exec_nonzero,
                    state);

        REQUIRE(zero.taken);
        REQUIRE_FALSE(nonzero.taken);
    }

    SECTION("EXEC low dword nonzero") {
        state.exec = 0x10ULL;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::exec_zero,
                    state);
        const auto nonzero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::exec_nonzero,
                    state);

        REQUIRE_FALSE(zero.taken);
        REQUIRE(nonzero.taken);
    }

    SECTION("EXEC high dword nonzero") {
        state.exec = 0x0000000100000000ULL;

        const auto zero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::exec_zero,
                    state);
        const auto nonzero =
            astraea::graphics::
                evaluate_shader_branch_condition(
                    astraea::graphics::
                        ShaderIrBranchCondition::exec_nonzero,
                    state);

        REQUIRE_FALSE(zero.taken);
        REQUIRE(nonzero.taken);
    }
}


TEST_CASE(
    "CFG successor selection treats S_ENDPGM as terminal",
    "[graphics][shader-execution][cfg-successor]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(1, 0),
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

    const auto result =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                std::nullopt);

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->edge.has_value());
}

TEST_CASE(
    "CFG successor selection follows unconditional branch edge",
    "[graphics][shader-execution][cfg-successor][branch]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(2, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
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
    REQUIRE(graph->blocks.size() == 3);

    const auto result =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                std::nullopt);

    REQUIRE(result.has_value());
    REQUIRE(result->edge.has_value());
    REQUIRE(
        result->edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            unconditional_branch);
    REQUIRE(result->edge->target_block_index == 2);
}

TEST_CASE(
    "CFG successor selection chooses conditional taken and fallthrough edges",
    "[graphics][shader-execution][cfg-successor][conditional]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(5, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
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

    astraea::graphics::ShaderScalarState state{};
    state.scc = false;
    const auto not_taken =
        astraea::graphics::
            evaluate_shader_branch_condition(
                astraea::graphics::
                    ShaderIrBranchCondition::scc_one,
                state);
    const auto fallthrough =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                not_taken);

    REQUIRE(fallthrough.has_value());
    REQUIRE(fallthrough->edge.has_value());
    REQUIRE(
        fallthrough->edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            conditional_branch_fallthrough);
    REQUIRE(
        fallthrough->edge->target_block_index == 1);

    state.scc = true;
    const auto taken =
        astraea::graphics::
            evaluate_shader_branch_condition(
                astraea::graphics::
                    ShaderIrBranchCondition::scc_one,
                state);
    const auto branch =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                taken);

    REQUIRE(branch.has_value());
    REQUIRE(branch->edge.has_value());
    REQUIRE(
        branch->edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            conditional_branch_taken);
    REQUIRE(branch->edge->target_block_index == 2);
}

TEST_CASE(
    "CFG successor selection preserves ordinary linear fallthrough",
    "[graphics][shader-execution][cfg-successor][fallthrough]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(0, 0),
        make_sopp(0, 0),
        make_sopp(2, 0xfffeU),
        make_sopp(1, 0),
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
    REQUIRE(graph->blocks.size() == 3);

    const auto result =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                std::nullopt);

    REQUIRE(result.has_value());
    REQUIRE(result->edge.has_value());
    REQUIRE(
        result->edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            linear_fallthrough);
    REQUIRE(result->edge->target_block_index == 1);
}

TEST_CASE(
    "final conditional branch can fall off bounded stream when not taken",
    "[graphics][shader-execution][cfg-successor][conditional]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(4, 0xffffU),
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
    REQUIRE(graph->blocks.size() == 1);
    REQUIRE(graph->blocks[0].successors.size() == 1);

    astraea::graphics::ShaderScalarState state{};
    state.scc = true;
    const auto not_taken =
        astraea::graphics::
            evaluate_shader_branch_condition(
                astraea::graphics::
                    ShaderIrBranchCondition::scc_zero,
                state);
    const auto terminal =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                not_taken);

    REQUIRE(terminal.has_value());
    REQUIRE_FALSE(terminal->edge.has_value());

    state.scc = false;
    const auto taken =
        astraea::graphics::
            evaluate_shader_branch_condition(
                astraea::graphics::
                    ShaderIrBranchCondition::scc_zero,
                state);
    const auto loop =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                taken);

    REQUIRE(loop.has_value());
    REQUIRE(loop->edge.has_value());
    REQUIRE(
        loop->edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            conditional_branch_taken);
    REQUIRE(loop->edge->target_block_index == 0);
}

TEST_CASE(
    "CFG successor selection requires matching conditional decision",
    "[graphics][shader-execution][cfg-successor][validation]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(5, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
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

    const auto missing =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                std::nullopt);
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(
        missing.error().code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                branch_decision_required);

    const astraea::graphics::ShaderBranchDecision mismatch{
        .condition =
            astraea::graphics::
                ShaderIrBranchCondition::scc_zero,
        .taken = true,
    };
    const auto wrong =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                mismatch);
    REQUIRE_FALSE(wrong.has_value());
    REQUIRE(
        wrong.error().code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                branch_condition_mismatch);
}

TEST_CASE(
    "CFG successor selection rejects invalid edge targets before selection",
    "[graphics][shader-execution][cfg-successor][validation]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(2, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
    };
    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());
    REQUIRE(graph.has_value());

    graph->blocks[0]
        .successors[0]
        .target_block_index =
        graph->blocks.size();

    const auto result =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                std::nullopt);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                invalid_edge_target);
    REQUIRE(
        result.error().edge_index ==
        std::optional<std::size_t>{0});
}

TEST_CASE(
    "CFG successor selection rejects malformed edge topology",
    "[graphics][shader-execution][cfg-successor][validation]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(2, 1),
        make_sopp(0, 0),
        make_sopp(1, 0),
    };
    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());
    REQUIRE(graph.has_value());

    graph->blocks[0].successors[0].kind =
        astraea::graphics::ShaderCfgEdgeKind::
            linear_fallthrough;

    const auto result =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                std::nullopt);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                invalid_successor_topology);
}

TEST_CASE(
    "CFG successor selection rejects graph program mismatch and invalid block",
    "[graphics][shader-execution][cfg-successor][validation]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(1, 0),
    };
    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());
    REQUIRE(graph.has_value());

    SECTION("graph program mismatch") {
        ++graph->emission_count;
        const auto result =
            astraea::graphics::
                select_shader_cfg_successor(
                    program.value(),
                    graph.value(),
                    0,
                    std::nullopt);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderCfgSuccessorErrorCode::
                    graph_program_mismatch);
    }

    SECTION("block index out of bounds") {
        const auto result =
            astraea::graphics::
                select_shader_cfg_successor(
                    program.value(),
                    graph.value(),
                    graph->blocks.size(),
                    std::nullopt);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderCfgSuccessorErrorCode::
                    block_index_out_of_bounds);
    }

    SECTION("zero-length block") {
        graph->blocks[0].emission_count = 0;
        const auto result =
            astraea::graphics::
                select_shader_cfg_successor(
                    program.value(),
                    graph.value(),
                    0,
                    std::nullopt);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderCfgSuccessorErrorCode::
                    invalid_block_extent);
    }
}

TEST_CASE(
    "CFG successor selection rejects branch decision on nonconditional exit",
    "[graphics][shader-execution][cfg-successor][validation]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(1, 0),
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

    const astraea::graphics::ShaderBranchDecision decision{
        .condition =
            astraea::graphics::
                ShaderIrBranchCondition::scc_zero,
        .taken = true,
    };
    const auto result =
        astraea::graphics::
            select_shader_cfg_successor(
                program.value(),
                graph.value(),
                0,
                decision);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                unexpected_branch_decision);
}
