#include <astraea/graphics/shader_vector_execution.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "V_MOV_B32 wave32 copies only active low-32 EXEC lanes",
    "[graphics][shader-execution][vector][mov32][wave32]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 0U) |
        (std::uint64_t{1} << 31U) |
        (std::uint64_t{1} << 40U);

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    vector_state.vgprs[2][0] = 0x11111111U;
    vector_state.vgprs[2][1] = 0x22222222U;
    vector_state.vgprs[2][31] = 0x33333333U;
    vector_state.vgprs[2][40] = 0x44444444U;
    vector_state.vgprs[1][0] = 0xaaaaaaaaU;
    vector_state.vgprs[1][1] = 0xbbbbbbbbU;
    vector_state.vgprs[1][31] = 0xccccccccU;
    vector_state.vgprs[1][40] = 0xddddddddU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorMove32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 1,
                },
            .source =
                astraea::graphics::ShaderIrVgpr{
                    .index = 2,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->wave_size ==
        astraea::graphics::ShaderWaveSize::wave32);
    REQUIRE(result->destination_vgpr == 1);
    REQUIRE(result->source_vgpr == 2);
    REQUIRE(
        result->active_lane_mask ==
        ((std::uint64_t{1} << 0U) |
         (std::uint64_t{1} << 31U)));
    REQUIRE(vector_state.vgprs[1][0] == 0x11111111U);
    REQUIRE(vector_state.vgprs[1][1] == 0xbbbbbbbbU);
    REQUIRE(vector_state.vgprs[1][31] == 0x33333333U);
    REQUIRE(vector_state.vgprs[1][40] == 0xddddddddU);
    REQUIRE(result->written_values[0] == 0x11111111U);
    REQUIRE(result->written_values[31] == 0x33333333U);
    REQUIRE(result->written_values[40] == 0U);
}

TEST_CASE(
    "V_MOV_B32 wave64 copies active EXEC lanes across the full mask",
    "[graphics][shader-execution][vector][mov32][wave64]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 0U) |
        (std::uint64_t{1} << 40U) |
        (std::uint64_t{1} << 63U);

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave64;
    vector_state.vgprs[9][0] = 0x01020304U;
    vector_state.vgprs[9][40] = 0x11223344U;
    vector_state.vgprs[9][63] = 0xaabbccddU;
    vector_state.vgprs[8][1] = 0xfeedfaceU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorMove32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 8,
                },
            .source =
                astraea::graphics::ShaderIrVgpr{
                    .index = 9,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->active_lane_mask ==
        scalar_state.exec);
    REQUIRE(vector_state.vgprs[8][0] == 0x01020304U);
    REQUIRE(vector_state.vgprs[8][1] == 0xfeedfaceU);
    REQUIRE(vector_state.vgprs[8][40] == 0x11223344U);
    REQUIRE(vector_state.vgprs[8][63] == 0xaabbccddU);
}

TEST_CASE(
    "V_MOV_B32 with zero EXEC preserves all destination lanes",
    "[graphics][shader-execution][vector][mov32][exec]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave64;
    vector_state.vgprs[3][0] = 0x12345678U;
    vector_state.vgprs[4][0] = 0x87654321U;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorMove32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 3,
                },
            .source =
                astraea::graphics::ShaderIrVgpr{
                    .index = 4,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(result->active_lane_mask == 0);
    REQUIRE(vector_state.vgprs[3][0] == 0x12345678U);
}

TEST_CASE(
    "V_MOV_B32 source equal to destination is deterministic",
    "[graphics][shader-execution][vector][mov32][alias]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = std::uint64_t{1} << 7U;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    vector_state.vgprs[5][7] = 0xdeadbeefU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorMove32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 5,
                },
            .source =
                astraea::graphics::ShaderIrVgpr{
                    .index = 5,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(vector_state.vgprs[5][7] == 0xdeadbeefU);
    REQUIRE(result->written_values[7] == 0xdeadbeefU);
}

TEST_CASE(
    "V_MOV_B32 rejects unspecified wave size without mutation",
    "[graphics][shader-execution][vector][validation]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.vgprs[1][0] = 0x11111111U;
    vector_state.vgprs[2][0] = 0x22222222U;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorMove32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 1,
                },
            .source =
                astraea::graphics::ShaderIrVgpr{
                    .index = 2,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderVectorExecutionErrorCode::
                invalid_wave_size);
    REQUIRE(vector_state.vgprs[1][0] == 0x11111111U);
    REQUIRE(vector_state.vgprs[2][0] == 0x22222222U);
}

TEST_CASE(
    "vector executor rejects unsupported Shader IR without mutation",
    "[graphics][shader-execution][vector][unsupported]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    vector_state.vgprs[1][0] = 0x11111111U;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrNop{
            .repeat_count = 1,
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderVectorExecutionErrorCode::
                unsupported_operation);
    REQUIRE(vector_state.vgprs[1][0] == 0x11111111U);
}
