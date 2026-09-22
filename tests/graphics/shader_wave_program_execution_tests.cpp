#include <astraea/graphics/shader_wave_program_execution.hpp>

#include <array>
#include <cstdint>
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
    "bounded mixed wave program executes one terminal mixed block",
    "[graphics][shader-execution][wave-program][mixed]") {
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
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[3][0] = 0x12345678U;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                1,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->entry_block_index == 0);
    REQUIRE(result->block_executions.size() == 1);
    REQUIRE(result->block_executions[0].effects.size() == 2);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderScalarExecutionEffect>(
            result->block_executions[0].effects[0]));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderVectorMove32Effect>(
            result->block_executions[0].effects[1]));
    REQUIRE(scalar_state.sgprs[1] == 1U);
    REQUIRE(vector_state.vgprs[2][0] == 0x12345678U);
}

TEST_CASE(
    "bounded mixed wave program traverses scalar block then vector block",
    "[graphics][shader-execution][wave-program][order]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 1, 129),
        make_sopp(2, 0),
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
    REQUIRE(graph->blocks.size() == 2);

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[3][0] = 0x11111111U;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                2,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->block_executions.size() == 2);
    REQUIRE(result->block_executions[0].block_index == 0);
    REQUIRE(result->block_executions[1].block_index == 1);
    REQUIRE(scalar_state.sgprs[1] == 1U);
    REQUIRE(vector_state.vgprs[2][0] == 0x11111111U);
}

TEST_CASE(
    "bounded mixed wave program traverses vector block then scalar block",
    "[graphics][shader-execution][wave-program][order]") {
    const std::array<std::uint32_t, 4> words{
        make_vop1(1, 4, 261),
        make_sopp(2, 0),
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
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                2,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->block_executions.size() == 2);
    REQUIRE(vector_state.vgprs[4][0] == 0xdeadbeefU);
    REQUIRE(scalar_state.sgprs[6] == 2U);
}

TEST_CASE(
    "bounded mixed wave program follows both conditional paths after mixed effects",
    "[graphics][shader-execution][wave-program][conditional]") {
    const std::array<std::uint32_t, 6> words{
        make_sop1(3, 7, 131),
        make_vop1(1, 8, 265),
        make_sopp(5, 1),
        make_sopp(1, 0),
        make_sop1(3, 10, 132),
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
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        scalar_state.scc = false;
        auto vector_state = wave32_state();
        vector_state.vgprs[9][0] = 0xaabbccddU;

        const auto result =
            astraea::graphics::
                execute_shader_wave_program(
                    program.value(),
                    graph.value(),
                    0,
                    2,
                    scalar_state,
                    vector_state);

        REQUIRE(result.has_value());
        REQUIRE(result->block_executions.size() == 2);
        REQUIRE(result->block_executions[1].block_index == 1);
        REQUIRE(scalar_state.sgprs[7] == 3U);
        REQUIRE(scalar_state.sgprs[10] == 0U);
        REQUIRE(vector_state.vgprs[8][0] == 0xaabbccddU);
    }

    SECTION("taken") {
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        scalar_state.scc = true;
        auto vector_state = wave32_state();
        vector_state.vgprs[9][0] = 0x01020304U;

        const auto result =
            astraea::graphics::
                execute_shader_wave_program(
                    program.value(),
                    graph.value(),
                    0,
                    2,
                    scalar_state,
                    vector_state);

        REQUIRE(result.has_value());
        REQUIRE(result->block_executions.size() == 2);
        REQUIRE(result->block_executions[1].block_index == 2);
        REQUIRE(scalar_state.sgprs[7] == 3U);
        REQUIRE(scalar_state.sgprs[10] == 4U);
        REQUIRE(vector_state.vgprs[8][0] == 0x01020304U);
    }
}

TEST_CASE(
    "bounded mixed wave program accepts explicit nonzero entry block",
    "[graphics][shader-execution][wave-program][entry]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 1, 129),
        make_sopp(2, 0),
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
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[3][0] = 0x55667788U;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                1,
                1,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->entry_block_index == 1);
    REQUIRE(result->block_executions.size() == 1);
    REQUIRE(result->block_executions[0].block_index == 1);
    REQUIRE(scalar_state.sgprs[1] == 0U);
    REQUIRE(vector_state.vgprs[2][0] == 0x55667788U);
}

TEST_CASE(
    "wave32 EXEC masking remains correct after a block transition",
    "[graphics][shader-execution][wave-program][exec]") {
    const std::array<std::uint32_t, 3> words{
        make_sopp(2, 0),
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
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                2,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(vector_state.vgprs[7][2] == 0x01020304U);
    REQUIRE(vector_state.vgprs[7][40] == 0xfeedfaceU);
}

TEST_CASE(
    "bounded mixed wave program stops self-loop exactly at block budget",
    "[graphics][shader-execution][wave-program][budget][loop]") {
    const std::array<std::uint32_t, 2> words{
        make_vop1(1, 4, 261),
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

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();
    vector_state.vgprs[5][0] = 0xabcdef01U;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                3,
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderWaveProgramExecutionErrorCode::
                execution_budget_exhausted);
    REQUIRE(result.error().current_block_index == 0);
    REQUIRE(result.error().completed_blocks.size() == 3);
    REQUIRE(vector_state.vgprs[4][0] == 0xabcdef01U);
    REQUIRE_FALSE(result.error().block_error.has_value());
}

TEST_CASE(
    "bounded mixed wave program distinguishes zero and sufficient budget",
    "[graphics][shader-execution][wave-program][budget]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 1, 129),
        make_sopp(2, 0),
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

    SECTION("zero budget") {
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        auto vector_state = wave32_state();
        const auto before_scalar = scalar_state;
        const auto before_vector = vector_state;

        const auto result =
            astraea::graphics::
                execute_shader_wave_program(
                    program.value(),
                    graph.value(),
                    0,
                    0,
                    scalar_state,
                    vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderWaveProgramExecutionErrorCode::
                    execution_budget_exhausted);
        REQUIRE(result.error().completed_blocks.empty());
        REQUIRE(scalar_state == before_scalar);
        REQUIRE(vector_state == before_vector);
    }

    SECTION("exactly sufficient budget") {
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        auto vector_state = wave32_state();
        vector_state.vgprs[3][0] = 0x10203040U;

        const auto result =
            astraea::graphics::
                execute_shader_wave_program(
                    program.value(),
                    graph.value(),
                    0,
                    2,
                    scalar_state,
                    vector_state);

        REQUIRE(result.has_value());
        REQUIRE(result->block_executions.size() == 2);
        REQUIRE(scalar_state.sgprs[1] == 1U);
        REQUIRE(vector_state.vgprs[2][0] == 0x10203040U);
    }
}

TEST_CASE(
    "later mixed block failure preserves prior blocks and in-block vector progress",
    "[graphics][shader-execution][wave-program][partial-failure]") {
    const std::array<std::uint32_t, 5> words{
        make_sop1(3, 1, 129),
        make_sopp(2, 0),
        make_vop1(1, 2, 259),
        make_vop2(3, 4, 258, 3),
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
    vector_state.vgprs[3][0] = 0xabcdef01U;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                3,
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderWaveProgramExecutionErrorCode::
                block_execution_failure);
    REQUIRE(result.error().current_block_index == 1);
    REQUIRE(result.error().completed_blocks.size() == 1);
    REQUIRE(result.error().block_error.has_value());
    REQUIRE(
        result.error().block_error->code ==
        astraea::graphics::
            ShaderWaveBlockExecutionErrorCode::
                unsupported_operation);
    REQUIRE(
        result.error().block_error->
            completed_emission_count == 1);
    REQUIRE(scalar_state.sgprs[1] == 1U);
    REQUIRE(vector_state.vgprs[2][0] == 0xabcdef01U);
}

TEST_CASE(
    "later invalid wave size preserves earlier scalar block progress",
    "[graphics][shader-execution][wave-program][partial-failure]") {
    const std::array<std::uint32_t, 4> words{
        make_sop1(3, 5, 130),
        make_sopp(2, 0),
        make_vop1(1, 6, 263),
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
    vector_state.vgprs[7][0] = 0x12345678U;
    const auto before_vector = vector_state;

    const auto result =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                2,
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderWaveProgramExecutionErrorCode::
                block_execution_failure);
    REQUIRE(result.error().current_block_index == 1);
    REQUIRE(result.error().completed_blocks.size() == 1);
    REQUIRE(result.error().block_error.has_value());
    REQUIRE(
        result.error().block_error->code ==
        astraea::graphics::
            ShaderWaveBlockExecutionErrorCode::
                vector_execution_failure);
    REQUIRE(
        result.error().block_error->vector_error.has_value());
    REQUIRE(
        result.error().block_error->vector_error->code ==
        astraea::graphics::
            ShaderVectorExecutionErrorCode::
                invalid_wave_size);
    REQUIRE(scalar_state.sgprs[5] == 2U);
    REQUIRE(vector_state == before_vector);
}

TEST_CASE(
    "mixed wave program structural preflight is atomic",
    "[graphics][shader-execution][wave-program][validation]") {
    const std::array<std::uint32_t, 3> words{
        make_sop1(3, 8, 131),
        make_vop1(1, 9, 266),
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
    vector_state.vgprs[10][0] = 0xaaaaaaaaU;
    const auto before_scalar = scalar_state;
    const auto before_vector = vector_state;

    SECTION("graph program mismatch") {
        ++graph->emission_count;

        const auto result =
            astraea::graphics::
                execute_shader_wave_program(
                    program.value(),
                    graph.value(),
                    0,
                    1,
                    scalar_state,
                    vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderWaveProgramExecutionErrorCode::
                    graph_program_mismatch);
        REQUIRE(result.error().completed_blocks.empty());
        REQUIRE(scalar_state == before_scalar);
        REQUIRE(vector_state == before_vector);
    }

    SECTION("entry block out of bounds") {
        const auto result =
            astraea::graphics::
                execute_shader_wave_program(
                    program.value(),
                    graph.value(),
                    graph->blocks.size(),
                    1,
                    scalar_state,
                    vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderWaveProgramExecutionErrorCode::
                    entry_block_out_of_bounds);
        REQUIRE(result.error().completed_blocks.empty());
        REQUIRE(scalar_state == before_scalar);
        REQUIRE(vector_state == before_vector);
    }
}
