#include <astraea/graphics/shader_program_execution.hpp>

#include <array>
#include <cstdint>

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
    "bounded scalar program executes one terminal block",
    "[graphics][shader-execution][program][scalar]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 1, 129),
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
            execute_shader_scalar_program(
                program.value(),
                graph.value(),
                0,
                1,
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->entry_block_index == 0);
    REQUIRE(result->block_executions.size() == 1);
    REQUIRE(
        result->block_executions[0].block_index == 0);
    REQUIRE(state.sgprs[1] == 1U);
}

TEST_CASE(
    "bounded scalar program follows unconditional branch and skips unreachable block",
    "[graphics][shader-execution][program][branch]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(2, 1),
        make_sop1(3, 1, 129),
        make_sop1(3, 2, 130),
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
            execute_shader_scalar_program(
                program.value(),
                graph.value(),
                0,
                2,
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->block_executions.size() == 2);
    REQUIRE(
        result->block_executions[0].block_index == 0);
    REQUIRE(
        result->block_executions[1].block_index == 2);
    REQUIRE(state.sgprs[1] == 0U);
    REQUIRE(state.sgprs[2] == 2U);
}

TEST_CASE(
    "bounded scalar program follows both conditional paths",
    "[graphics][shader-execution][program][conditional]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(5, 1),
        make_sopp(1, 0),
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

    SECTION("not taken") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = false;

        const auto result =
            astraea::graphics::
                execute_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    0,
                    2,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(
            result->block_executions.size() == 2);
        REQUIRE(
            result->block_executions[1].block_index ==
            1);
        REQUIRE(state.sgprs[3] == 0U);
    }

    SECTION("taken") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = true;

        const auto result =
            astraea::graphics::
                execute_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    0,
                    2,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(
            result->block_executions.size() == 2);
        REQUIRE(
            result->block_executions[1].block_index ==
            2);
        REQUIRE(state.sgprs[3] == 3U);
    }
}

TEST_CASE(
    "bounded scalar program accepts explicit nonzero entry block",
    "[graphics][shader-execution][program][entry]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(2, 1),
        make_sop1(3, 1, 129),
        make_sop1(3, 2, 130),
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
            execute_shader_scalar_program(
                program.value(),
                graph.value(),
                1,
                2,
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->entry_block_index == 1);
    REQUIRE(result->block_executions.size() == 2);
    REQUIRE(
        result->block_executions[0].block_index == 1);
    REQUIRE(
        result->block_executions[1].block_index == 2);
    REQUIRE(state.sgprs[1] == 1U);
    REQUIRE(state.sgprs[2] == 2U);
}

TEST_CASE(
    "bounded scalar program stops a self-loop exactly at block budget",
    "[graphics][shader-execution][program][budget][loop]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 4, 132),
        make_sopp(2, 0xfffeU),
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
            execute_shader_scalar_program(
                program.value(),
                graph.value(),
                0,
                3,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                execution_budget_exhausted);
    REQUIRE(result.error().current_block_index == 0);
    REQUIRE(
        result.error().completed_blocks.size() == 3);
    REQUIRE(state.sgprs[4] == 4U);
    REQUIRE_FALSE(result.error().block_error.has_value());
}

TEST_CASE(
    "bounded scalar program zero budget performs no execution",
    "[graphics][shader-execution][program][budget]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 5, 133),
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
            execute_shader_scalar_program(
                program.value(),
                graph.value(),
                0,
                0,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                execution_budget_exhausted);
    REQUIRE(result.error().completed_blocks.empty());
    REQUIRE(state == before);
}

TEST_CASE(
    "bounded scalar program distinguishes sufficient and exhausted budget",
    "[graphics][shader-execution][program][budget]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(2, 1),
        make_sop1(3, 1, 129),
        make_sop1(3, 2, 130),
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

    SECTION("exactly sufficient") {
        astraea::graphics::ShaderScalarState state{};
        const auto result =
            astraea::graphics::
                execute_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    0,
                    2,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(
            result->block_executions.size() == 2);
        REQUIRE(state.sgprs[2] == 2U);
    }

    SECTION("one block short") {
        astraea::graphics::ShaderScalarState state{};
        const auto result =
            astraea::graphics::
                execute_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    0,
                    1,
                    state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderScalarProgramExecutionErrorCode::
                    execution_budget_exhausted);
        REQUIRE(
            result.error().completed_blocks.size() == 1);
        REQUIRE(
            result.error().current_block_index == 2);
        REQUIRE(state.sgprs[2] == 0U);
    }
}

TEST_CASE(
    "bounded scalar program preserves prior blocks when later block fails",
    "[graphics][shader-execution][program][partial-failure]") {
    const std::array<std::uint32_t, 4> words{
        make_sopp(5, 2),
        make_sop1(3, 6, 134),
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
    state.scc = false;
    const auto result =
        astraea::graphics::
            execute_shader_scalar_program(
                program.value(),
                graph.value(),
                0,
                3,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarProgramExecutionErrorCode::
                block_execution_failure);
    REQUIRE(result.error().current_block_index == 1);
    REQUIRE(
        result.error().completed_blocks.size() == 1);
    REQUIRE(result.error().block_error.has_value());
    REQUIRE(
        result.error().block_error->code ==
        astraea::graphics::
            ShaderScalarBlockExecutionErrorCode::
                unsupported_operation);
    REQUIRE(
        result.error().block_error->
            completed_emission_count == 1);
    REQUIRE(state.sgprs[6] == 6U);
}

TEST_CASE(
    "bounded scalar program structural preflight is atomic",
    "[graphics][shader-execution][program][validation]") {
    const std::array<std::uint32_t, 2> words{
        make_sop1(3, 7, 135),
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
                execute_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    0,
                    1,
                    state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderScalarProgramExecutionErrorCode::
                    graph_program_mismatch);
        REQUIRE(
            result.error().completed_blocks.empty());
        REQUIRE(state == before);
    }

    SECTION("entry block out of bounds") {
        const auto result =
            astraea::graphics::
                execute_shader_scalar_program(
                    program.value(),
                    graph.value(),
                    graph->blocks.size(),
                    1,
                    state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderScalarProgramExecutionErrorCode::
                    entry_block_out_of_bounds);
        REQUIRE(
            result.error().completed_blocks.empty());
        REQUIRE(state == before);
    }
}
