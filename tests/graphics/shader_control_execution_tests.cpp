#include <astraea/graphics/shader_control_execution.hpp>

#include <array>
#include <cstdint>
#include <optional>

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


TEST_CASE(
    "scalar block execution applies ordered moves and terminates at S_ENDPGM",
    "[graphics][shader-execution][block][scalar]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 1, 129),
        make_sop1(3, 2, 1),
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
    const auto result =
        astraea::graphics::
            execute_shader_scalar_block(
                program.value(),
                graph.value(),
                0,
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->block_index == 0);
    REQUIRE(result->executed_emission_count == 3);
    REQUIRE(result->scalar_write_effects.size() == 2);
    REQUIRE(state.sgprs[1] == 1U);
    REQUIRE(state.sgprs[2] == 1U);
    REQUIRE(
        result->scalar_write_effects[0]
            .first_destination_sgpr == 1);
    REQUIRE(
        result->scalar_write_effects[1]
            .first_destination_sgpr == 2);
    REQUIRE_FALSE(result->branch_decision.has_value());
    REQUIRE_FALSE(result->successor.edge.has_value());
}

TEST_CASE(
    "scalar block execution returns unconditional branch successor without executing it",
    "[graphics][shader-execution][block][branch]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 4, 130),
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

    astraea::graphics::ShaderScalarState state{};
    const auto result =
        astraea::graphics::
            execute_shader_scalar_block(
                program.value(),
                graph.value(),
                0,
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->executed_emission_count == 2);
    REQUIRE(result->scalar_write_effects.size() == 1);
    REQUIRE(state.sgprs[4] == 2U);
    REQUIRE_FALSE(result->branch_decision.has_value());
    REQUIRE(result->successor.edge.has_value());
    REQUIRE(
        result->successor.edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            unconditional_branch);
    REQUIRE(
        result->successor.edge->target_block_index == 2);
    REQUIRE(state.sgprs[0] == 0U);
}

TEST_CASE(
    "scalar block execution evaluates conditional exit after prior emissions",
    "[graphics][shader-execution][block][conditional]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 5, 131),
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

    SECTION("not taken") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = false;

        const auto result =
            astraea::graphics::
                execute_shader_scalar_block(
                    program.value(),
                    graph.value(),
                    0,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(state.sgprs[5] == 3U);
        REQUIRE(result->branch_decision.has_value());
        REQUIRE_FALSE(result->branch_decision->taken);
        REQUIRE(result->successor.edge.has_value());
        REQUIRE(
            result->successor.edge->kind ==
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_fallthrough);
        REQUIRE(
            result->successor.edge->target_block_index == 1);
    }

    SECTION("taken") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = true;

        const auto result =
            astraea::graphics::
                execute_shader_scalar_block(
                    program.value(),
                    graph.value(),
                    0,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(state.sgprs[5] == 3U);
        REQUIRE(result->branch_decision.has_value());
        REQUIRE(result->branch_decision->taken);
        REQUIRE(result->successor.edge.has_value());
        REQUIRE(
            result->successor.edge->kind ==
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_taken);
        REQUIRE(
            result->successor.edge->target_block_index == 2);
    }
}

TEST_CASE(
    "scalar block execution returns ordinary linear fallthrough",
    "[graphics][shader-execution][block][fallthrough]") {
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

    astraea::graphics::ShaderScalarState state{};
    const auto result =
        astraea::graphics::
            execute_shader_scalar_block(
                program.value(),
                graph.value(),
                0,
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->executed_emission_count == 1);
    REQUIRE(result->scalar_write_effects.empty());
    REQUIRE_FALSE(result->branch_decision.has_value());
    REQUIRE(result->successor.edge.has_value());
    REQUIRE(
        result->successor.edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            linear_fallthrough);
    REQUIRE(
        result->successor.edge->target_block_index == 1);
}

TEST_CASE(
    "scalar block execution preserves prior writes on later unsupported operation",
    "[graphics][shader-execution][block][partial-failure]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 6, 132),
        make_vop1(1, 1, 258),
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
    const auto result =
        astraea::graphics::
            execute_shader_scalar_block(
                program.value(),
                graph.value(),
                0,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarBlockExecutionErrorCode::
                unsupported_operation);
    REQUIRE(result.error().emission_index == 1);
    REQUIRE(
        result.error().completed_emission_count == 1);
    REQUIRE(state.sgprs[6] == 4U);
    REQUIRE_FALSE(result.error().scalar_error.has_value());
    REQUIRE_FALSE(
        result.error().successor_error.has_value());
}

TEST_CASE(
    "scalar block execution forwards scalar execution failure without mutation",
    "[graphics][shader-execution][block][validation]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 1, 129),
        make_sopp(1, 0),
    };
    auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    program->emissions[0].operation =
        astraea::graphics::ShaderIrScalarMove32{
            .destination =
                astraea::graphics::ShaderIrSgpr{
                    .index = 106,
                },
            .source =
                astraea::graphics::ShaderIrInlineInteger32{
                    .value = 7,
                },
        };

    const auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());
    REQUIRE(graph.has_value());

    astraea::graphics::ShaderScalarState state{};
    state.sgprs[1] = 0xabcdef01U;
    const auto before = state;

    const auto result =
        astraea::graphics::
            execute_shader_scalar_block(
                program.value(),
                graph.value(),
                0,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarBlockExecutionErrorCode::
                scalar_execution_failure);
    REQUIRE(result.error().emission_index == 0);
    REQUIRE(
        result.error().completed_emission_count == 0);
    REQUIRE(result.error().scalar_error.has_value());
    REQUIRE(
        result.error().scalar_error->code ==
        astraea::graphics::
            ShaderScalarExecutionErrorCode::
                invalid_sgpr_index);
    REQUIRE(state == before);
}

TEST_CASE(
    "scalar block execution forwards successor failure after completed writes",
    "[graphics][shader-execution][block][partial-failure]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 7, 133),
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

    astraea::graphics::ShaderScalarState state{};
    const auto result =
        astraea::graphics::
            execute_shader_scalar_block(
                program.value(),
                graph.value(),
                0,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarBlockExecutionErrorCode::
                cfg_successor_failure);
    REQUIRE(
        result.error().completed_emission_count == 2);
    REQUIRE(state.sgprs[7] == 5U);
    REQUIRE(result.error().successor_error.has_value());
    REQUIRE(
        result.error().successor_error->code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                invalid_successor_topology);
}

TEST_CASE(
    "scalar block execution rejects structural mismatch before mutation",
    "[graphics][shader-execution][block][validation]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 8, 134),
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

    astraea::graphics::ShaderScalarState state{};
    const auto before = state;

    SECTION("graph program mismatch") {
        ++graph->emission_count;
        const auto result =
            astraea::graphics::
                execute_shader_scalar_block(
                    program.value(),
                    graph.value(),
                    0,
                    state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderScalarBlockExecutionErrorCode::
                    graph_program_mismatch);
        REQUIRE(state == before);
    }

    SECTION("invalid block extent") {
        graph->blocks[0].emission_count = 0;
        const auto result =
            astraea::graphics::
                execute_shader_scalar_block(
                    program.value(),
                    graph.value(),
                    0,
                    state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderScalarBlockExecutionErrorCode::
                    invalid_block_extent);
        REQUIRE(state == before);
    }
}


TEST_CASE(
    "bounded scalar runner treats empty program as zero-block termination",
    "[graphics][shader-execution][program][bounded]") {
    const astraea::graphics::ShaderIrProgram program{};
    const auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(program);
    REQUIRE(graph.has_value());

    astraea::graphics::ShaderScalarState state{};
    state.sgprs[0] = 0x12345678U;
    const auto before = state;

    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program,
                graph.value(),
                state,
                0);

    REQUIRE(result.has_value());
    REQUIRE(result->executed_block_count == 0);
    REQUIRE(result->executed_emission_count == 0);
    REQUIRE(result->block_executions.empty());
    REQUIRE(state == before);
}

TEST_CASE(
    "bounded scalar runner follows unconditional CFG and skips unreachable source block",
    "[graphics][shader-execution][program][bounded][branch]") {
    const std::array<std::uint32_t, 5> words{
        make_sop1(3, 1, 129),
        make_sopp(2, 1),
        make_sop1(3, 2, 130),
        make_sop1(3, 3, 131),
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

    astraea::graphics::ShaderScalarState state{};
    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program.value(),
                graph.value(),
                state,
                4);

    REQUIRE(result.has_value());
    REQUIRE(result->executed_block_count == 2);
    REQUIRE(result->executed_emission_count == 4);
    REQUIRE(result->block_executions.size() == 2);
    REQUIRE(result->block_executions[0].block_index == 0);
    REQUIRE(result->block_executions[1].block_index == 2);
    REQUIRE(state.sgprs[1] == 1U);
    REQUIRE(state.sgprs[2] == 0U);
    REQUIRE(state.sgprs[3] == 3U);
}

TEST_CASE(
    "bounded scalar runner follows both conditional paths",
    "[graphics][shader-execution][program][bounded][conditional]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(5, 1),
        make_sop1(3, 4, 129),
        make_sop1(3, 5, 130),
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

    SECTION("fallthrough path") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = false;

        const auto result =
            astraea::graphics::
                run_bounded_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    state,
                    4);

        REQUIRE(result.has_value());
        REQUIRE(result->executed_block_count == 3);
        REQUIRE(result->block_executions[0].block_index == 0);
        REQUIRE(result->block_executions[1].block_index == 1);
        REQUIRE(result->block_executions[2].block_index == 2);
        REQUIRE(state.sgprs[4] == 1U);
        REQUIRE(state.sgprs[5] == 2U);
    }

    SECTION("taken path") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = true;

        const auto result =
            astraea::graphics::
                run_bounded_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    state,
                    3);

        REQUIRE(result.has_value());
        REQUIRE(result->executed_block_count == 2);
        REQUIRE(result->block_executions[0].block_index == 0);
        REQUIRE(result->block_executions[1].block_index == 2);
        REQUIRE(state.sgprs[4] == 0U);
        REQUIRE(state.sgprs[5] == 2U);
    }
}

TEST_CASE(
    "bounded scalar runner stops a self-loop at the explicit block budget",
    "[graphics][shader-execution][program][bounded][loop]") {
    const std::array<std::uint32_t, 1> words{
        make_sopp(2, 0xffffU),
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

    astraea::graphics::ShaderScalarState state{};
    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program.value(),
                graph.value(),
                state,
                3);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                execution_budget_exhausted);
    REQUIRE(result.error().next_block_index == 0);
    REQUIRE(result.error().completed_block_count == 3);
    REQUIRE(result.error().completed_emission_count == 3);
    REQUIRE_FALSE(result.error().block_error.has_value());
}

TEST_CASE(
    "bounded scalar runner enforces zero budget before first nonempty block",
    "[graphics][shader-execution][program][bounded][budget]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 6, 132),
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
    const auto before = state;
    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program.value(),
                graph.value(),
                state,
                0);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                execution_budget_exhausted);
    REQUIRE(result.error().next_block_index == 0);
    REQUIRE(result.error().completed_block_count == 0);
    REQUIRE(result.error().completed_emission_count == 0);
    REQUIRE(state == before);
}

TEST_CASE(
    "bounded scalar runner forwards downstream block failure after earlier block writes",
    "[graphics][shader-execution][program][bounded][partial-failure]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 7, 133),
        make_sopp(2, 0),
        make_vop1(1, 1, 258),
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
    REQUIRE(graph->blocks.size() == 2);

    astraea::graphics::ShaderScalarState state{};
    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program.value(),
                graph.value(),
                state,
                3);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                block_execution_failure);
    REQUIRE(result.error().next_block_index == 1);
    REQUIRE(result.error().completed_block_count == 1);
    REQUIRE(result.error().completed_emission_count == 2);
    REQUIRE(result.error().block_error.has_value());
    REQUIRE(
        result.error().block_error->code ==
        astraea::graphics::
            ShaderScalarBlockExecutionErrorCode::
                unsupported_operation);
    REQUIRE(result.error().block_error->emission_index == 2);
    REQUIRE(
        result.error().block_error->
            completed_emission_count == 0);
    REQUIRE(state.sgprs[7] == 5U);
}

TEST_CASE(
    "bounded scalar runner forwards malformed entry block before state mutation",
    "[graphics][shader-execution][program][bounded][validation]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 8, 134),
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
    graph->blocks[0].emission_count = 0;

    astraea::graphics::ShaderScalarState state{};
    const auto before = state;
    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program.value(),
                graph.value(),
                state,
                2);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                block_execution_failure);
    REQUIRE(result.error().next_block_index == 0);
    REQUIRE(result.error().completed_block_count == 0);
    REQUIRE(result.error().completed_emission_count == 0);
    REQUIRE(result.error().block_error.has_value());
    REQUIRE(
        result.error().block_error->code ==
        astraea::graphics::
            ShaderScalarBlockExecutionErrorCode::
                invalid_block_extent);
    REQUIRE(state == before);
}

TEST_CASE(
    "bounded scalar runner rejects graph program mismatch before mutation",
    "[graphics][shader-execution][program][bounded][validation]") {
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
    ++graph->emission_count;

    astraea::graphics::ShaderScalarState state{};
    state.sgprs[0] = 42U;
    const auto before = state;

    const auto result =
        astraea::graphics::
            run_bounded_shader_scalar_program(
                program.value(),
                graph.value(),
                state,
                2);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                graph_program_mismatch);
    REQUIRE(state == before);
}
