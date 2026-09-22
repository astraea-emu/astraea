#include <astraea/graphics/shader_wave_block_execution.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

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

astraea::graphics::ShaderVectorState wave32_state() {
    astraea::graphics::ShaderVectorState state{};
    state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    return state;
}

}  // namespace

TEST_CASE(
    "mixed block preserves scalar then vector execution order",
    "[graphics][shader-execution][wave-block][order]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 1, 129),
        make_vop1(1, 2, 259),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0x3;
    auto vector_state = wave32_state();
    vector_state.vgprs[3][0] = 0x11111111U;
    vector_state.vgprs[3][1] = 0x22222222U;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->executed_emission_count == 3);
    REQUIRE(result->effects.size() == 2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderScalarExecutionEffect>(
            result->effects[0]));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderVectorMove32Effect>(
            result->effects[1]));
    REQUIRE(scalar_state.sgprs[1] == 1U);
    REQUIRE(vector_state.vgprs[2][0] == 0x11111111U);
    REQUIRE(vector_state.vgprs[2][1] == 0x22222222U);
    REQUIRE_FALSE(result->branch_decision.has_value());
    REQUIRE_FALSE(result->successor.edge.has_value());
}

TEST_CASE(
    "mixed block preserves vector then scalar execution order",
    "[graphics][shader-execution][wave-block][order]") {
    const std::array<std::uint32_t, 3> words{
        make_vop1(1, 4, 261),
        make_sop1(3, 6, 130),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[5][0] = 0xdeadbeefU;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->effects.size() == 2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderVectorMove32Effect>(
            result->effects[0]));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderScalarExecutionEffect>(
            result->effects[1]));
    REQUIRE(vector_state.vgprs[4][0] == 0xdeadbeefU);
    REQUIRE(scalar_state.sgprs[6] == 2U);
}

TEST_CASE(
    "mixed block returns unconditional CFG successor without executing it",
    "[graphics][shader-execution][wave-block][branch]") {
    const std::array<std::uint32_t, 4> words{
        make_vop1(1, 1, 258),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[2][0] = 0x12345678U;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->executed_emission_count == 2);
    REQUIRE(result->effects.size() == 1);
    REQUIRE(vector_state.vgprs[1][0] == 0x12345678U);
    REQUIRE(result->successor.edge.has_value());
    REQUIRE(
        result->successor.edge->kind ==
        astraea::graphics::ShaderCfgEdgeKind::
            unconditional_branch);
    REQUIRE(
        result->successor.edge->target_block_index == 2);
}

TEST_CASE(
    "mixed block evaluates conditional exit after vector write",
    "[graphics][shader-execution][wave-block][conditional]") {
    const std::array<std::uint32_t, 4> words{
        make_vop1(1, 1, 258),
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
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        scalar_state.scc = false;
        auto vector_state = wave32_state();
        vector_state.vgprs[2][0] = 0x11111111U;

        const auto result =
            astraea::graphics::execute_shader_wave_block(
                program.value(),
                graph.value(),
                0,
                scalar_state,
                vector_state);

        REQUIRE(result.has_value());
        REQUIRE(result->branch_decision.has_value());
        REQUIRE_FALSE(result->branch_decision->taken);
        REQUIRE(result->successor.edge.has_value());
        REQUIRE(
            result->successor.edge->kind ==
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_fallthrough);
        REQUIRE(
            result->successor.edge->target_block_index == 1);
        REQUIRE(vector_state.vgprs[1][0] == 0x11111111U);
    }

    SECTION("taken") {
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        scalar_state.scc = true;
        auto vector_state = wave32_state();
        vector_state.vgprs[2][0] = 0x22222222U;

        const auto result =
            astraea::graphics::execute_shader_wave_block(
                program.value(),
                graph.value(),
                0,
                scalar_state,
                vector_state);

        REQUIRE(result.has_value());
        REQUIRE(result->branch_decision.has_value());
        REQUIRE(result->branch_decision->taken);
        REQUIRE(result->successor.edge.has_value());
        REQUIRE(
            result->successor.edge->kind ==
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_taken);
        REQUIRE(
            result->successor.edge->target_block_index == 2);
        REQUIRE(vector_state.vgprs[1][0] == 0x22222222U);
    }
}

TEST_CASE(
    "mixed block vector move obeys wave32 EXEC mask",
    "[graphics][shader-execution][wave-block][exec]") {
    const std::array<std::uint32_t, 2> words{
        make_vop1(1, 7, 264),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 2U) |
        (std::uint64_t{1} << 40U);
    auto vector_state = wave32_state();
    vector_state.vgprs[8][2] = 0x01020304U;
    vector_state.vgprs[8][40] = 0xaabbccddU;
    vector_state.vgprs[7][40] = 0xfeedfaceU;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE(result.has_value());
    REQUIRE(vector_state.vgprs[7][2] == 0x01020304U);
    REQUIRE(vector_state.vgprs[7][40] == 0xfeedfaceU);
    const auto& effect =
        std::get<
            astraea::graphics::ShaderVectorMove32Effect>(
            result->effects[0]);
    REQUIRE(
        effect.active_lane_mask ==
        (std::uint64_t{1} << 2U));
}

TEST_CASE(
    "mixed block preserves prior scalar and vector writes before unsupported V_ADD_F32",
    "[graphics][shader-execution][wave-block][partial-failure]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 9, 131),
        make_vop1(1, 10, 267),
        make_vop2(3, 12, 266, 11),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[11][0] = 0xabcdef01U;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderWaveBlockExecutionErrorCode::
                unsupported_operation);
    REQUIRE(result.error().emission_index == 2);
    REQUIRE(result.error().completed_emission_count == 2);
    REQUIRE(scalar_state.sgprs[9] == 3U);
    REQUIRE(vector_state.vgprs[10][0] == 0xabcdef01U);
    REQUIRE_FALSE(result.error().scalar_error.has_value());
    REQUIRE_FALSE(result.error().vector_error.has_value());
}

TEST_CASE(
    "mixed block forwards invalid wave mode after earlier scalar write",
    "[graphics][shader-execution][wave-block][partial-failure]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 12, 132),
        make_vop1(1, 13, 270),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.vgprs[14][0] = 0x12345678U;
    const auto before_vector = vector_state;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderWaveBlockExecutionErrorCode::
                vector_execution_failure);
    REQUIRE(result.error().emission_index == 1);
    REQUIRE(result.error().completed_emission_count == 1);
    REQUIRE(scalar_state.sgprs[12] == 4U);
    REQUIRE(result.error().vector_error.has_value());
    REQUIRE(
        result.error().vector_error->code ==
        astraea::graphics::
            ShaderVectorExecutionErrorCode::
                invalid_wave_size);
    REQUIRE(vector_state == before_vector);
}

TEST_CASE(
    "mixed block forwards CFG successor failure after completed effects",
    "[graphics][shader-execution][wave-block][partial-failure]") {
    const std::array<std::uint32_t, 5> words{
        make_sop1(3, 15, 133),
        make_vop1(1, 16, 273),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[17][0] = 0x13572468U;

    const auto result =
        astraea::graphics::execute_shader_wave_block(
            program.value(),
            graph.value(),
            0,
            scalar_state,
            vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderWaveBlockExecutionErrorCode::
                cfg_successor_failure);
    REQUIRE(result.error().completed_emission_count == 3);
    REQUIRE(scalar_state.sgprs[15] == 5U);
    REQUIRE(vector_state.vgprs[16][0] == 0x13572468U);
    REQUIRE(result.error().successor_error.has_value());
    REQUIRE(
        result.error().successor_error->code ==
        astraea::graphics::
            ShaderCfgSuccessorErrorCode::
                invalid_successor_topology);
}

TEST_CASE(
    "mixed block structural preflight fails before state mutation",
    "[graphics][shader-execution][wave-block][validation]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 18, 134),
        make_vop1(1, 19, 276),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[20][0] = 0xaaaaaaaaU;
    const auto before_scalar = scalar_state;
    const auto before_vector = vector_state;

    SECTION("graph program mismatch") {
        ++graph->emission_count;

        const auto result =
            astraea::graphics::execute_shader_wave_block(
                program.value(),
                graph.value(),
                0,
                scalar_state,
                vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderWaveBlockExecutionErrorCode::
                    graph_program_mismatch);
        REQUIRE(scalar_state == before_scalar);
        REQUIRE(vector_state == before_vector);
    }

    SECTION("zero-length block") {
        graph->blocks[0].emission_count = 0;

        const auto result =
            astraea::graphics::execute_shader_wave_block(
                program.value(),
                graph.value(),
                0,
                scalar_state,
                vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderWaveBlockExecutionErrorCode::
                    invalid_block_extent);
        REQUIRE(scalar_state == before_scalar);
        REQUIRE(vector_state == before_vector);
    }
}
