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


TEST_CASE(
    "exact V_ADD_F32 executes positive and negative normal sums",
    "[graphics][shader-execution][vector][add-f32][exact]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0x3;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;

    // lane 0: 1.0 + 1.0 = 2.0
    vector_state.vgprs[1][0] = 0x3f800000U;
    vector_state.vgprs[2][0] = 0x3f800000U;
    // lane 1: -1.0 + -2.0 = -3.0
    vector_state.vgprs[1][1] = 0xbf800000U;
    vector_state.vgprs[2][1] = 0xc0000000U;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorAddF32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 3,
                },
            .source0 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 1,
                },
            .source1 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 2,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->wave_size ==
        astraea::graphics::ShaderWaveSize::wave32);
    REQUIRE(result->destination_vgpr == 3);
    REQUIRE(result->source0_vgpr == 1);
    REQUIRE(result->source1_vgpr == 2);
    REQUIRE(result->active_lane_mask == 0x3);
    REQUIRE(vector_state.vgprs[3][0] == 0x40000000U);
    REQUIRE(vector_state.vgprs[3][1] == 0xc0400000U);
    REQUIRE(result->written_values[0] == 0x40000000U);
    REQUIRE(result->written_values[1] == 0xc0400000U);
}

TEST_CASE(
    "exact V_ADD_F32 supports active wave64 high lanes and preserves inactive lanes",
    "[graphics][shader-execution][vector][add-f32][wave64]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 40U);

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave64;
    vector_state.vgprs[4][40] = 0x3fc00000U;  // 1.5
    vector_state.vgprs[5][40] = 0x3f000000U;  // 0.5
    vector_state.vgprs[6][2] = 0xdeadbeefU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorAddF32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 6,
                },
            .source0 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 4,
                },
            .source1 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 5,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->active_lane_mask ==
        (std::uint64_t{1} << 40U));
    REQUIRE(vector_state.vgprs[6][40] == 0x40000000U);
    REQUIRE(vector_state.vgprs[6][2] == 0xdeadbeefU);
}

TEST_CASE(
    "exact V_ADD_F32 precomputation makes destination source alias deterministic",
    "[graphics][shader-execution][vector][add-f32][alias]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0x3;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    vector_state.vgprs[7][0] = 0x3f800000U;
    vector_state.vgprs[7][1] = 0x40000000U;
    vector_state.vgprs[8][0] = 0x3f800000U;
    vector_state.vgprs[8][1] = 0x3f800000U;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorAddF32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 7,
                },
            .source0 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 7,
                },
            .source1 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 8,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(vector_state.vgprs[7][0] == 0x40000000U);
    REQUIRE(vector_state.vgprs[7][1] == 0x40400000U);
}

TEST_CASE(
    "exact V_ADD_F32 rejects rounding-sensitive normal sum atomically",
    "[graphics][shader-execution][vector][add-f32][unsupported]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0x3;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;

    // lane 0 is executable: 1.0 + 1.0 = 2.0.
    vector_state.vgprs[1][0] = 0x3f800000U;
    vector_state.vgprs[2][0] = 0x3f800000U;
    // lane 1 is 1.0 + 2^-24: exact mathematical sum needs rounding.
    vector_state.vgprs[1][1] = 0x3f800000U;
    vector_state.vgprs[2][1] = 0x33800000U;

    vector_state.vgprs[3][0] = 0xaaaaaaaaU;
    vector_state.vgprs[3][1] = 0xbbbbbbbbU;
    const auto before = vector_state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorAddF32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 3,
                },
            .source0 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 1,
                },
            .source1 =
                astraea::graphics::ShaderIrVgpr{
                    .index = 2,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                operation,
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderVectorExecutionErrorCode::
                unsupported_f32_case);
    REQUIRE(
        result.error().lane_index ==
        std::optional<std::size_t>{1});
    REQUIRE(vector_state == before);
}

TEST_CASE(
    "exact V_ADD_F32 rejects deferred floating-point categories",
    "[graphics][shader-execution][vector][add-f32][unsupported]") {
    const auto run_unsupported =
        [](std::uint32_t left_bits,
           std::uint32_t right_bits) {
            astraea::graphics::ShaderScalarState scalar_state{};
            scalar_state.exec = 1;

            astraea::graphics::ShaderVectorState vector_state{};
            vector_state.wave_size =
                astraea::graphics::ShaderWaveSize::wave32;
            vector_state.vgprs[1][0] = left_bits;
            vector_state.vgprs[2][0] = right_bits;
            vector_state.vgprs[3][0] = 0x12345678U;

            const astraea::graphics::ShaderIrOperation operation =
                astraea::graphics::ShaderIrVectorAddF32{
                    .destination =
                        astraea::graphics::ShaderIrVgpr{
                            .index = 3,
                        },
                    .source0 =
                        astraea::graphics::ShaderIrVgpr{
                            .index = 1,
                        },
                    .source1 =
                        astraea::graphics::ShaderIrVgpr{
                            .index = 2,
                        },
                };

            const auto result =
                astraea::graphics::
                    execute_shader_vector_add_f32_exact_operation(
                        operation,
                        scalar_state,
                        vector_state);

            REQUIRE_FALSE(result.has_value());
            REQUIRE(
                result.error().code ==
                astraea::graphics::
                    ShaderVectorExecutionErrorCode::
                        unsupported_f32_case);
            REQUIRE(
                result.error().lane_index ==
                std::optional<std::size_t>{0});
            REQUIRE(vector_state.vgprs[3][0] == 0x12345678U);
        };

    SECTION("denormal input") {
        run_unsupported(0x00000001U, 0x3f800000U);
    }
    SECTION("infinity input") {
        run_unsupported(0x7f800000U, 0x3f800000U);
    }
    SECTION("NaN input") {
        run_unsupported(0x7fc00000U, 0x3f800000U);
    }
    SECTION("exact cancellation to zero") {
        run_unsupported(0x3f800000U, 0xbf800000U);
    }
    SECTION("overflow") {
        run_unsupported(0x7f000000U, 0x7f000000U);
    }
}

TEST_CASE(
    "exact V_ADD_F32 rejects invalid wave size and non-add IR without mutation",
    "[graphics][shader-execution][vector][add-f32][validation]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;

    SECTION("invalid wave size") {
        astraea::graphics::ShaderVectorState vector_state{};
        vector_state.vgprs[1][0] = 0x3f800000U;
        vector_state.vgprs[2][0] = 0x3f800000U;
        const auto before = vector_state;

        const astraea::graphics::ShaderIrOperation operation =
            astraea::graphics::ShaderIrVectorAddF32{
                .destination =
                    astraea::graphics::ShaderIrVgpr{
                        .index = 3,
                    },
                .source0 =
                    astraea::graphics::ShaderIrVgpr{
                        .index = 1,
                    },
                .source1 =
                    astraea::graphics::ShaderIrVgpr{
                        .index = 2,
                    },
            };

        const auto result =
            astraea::graphics::
                execute_shader_vector_add_f32_exact_operation(
                    operation,
                    scalar_state,
                    vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderVectorExecutionErrorCode::
                    invalid_wave_size);
        REQUIRE_FALSE(result.error().lane_index.has_value());
        REQUIRE(vector_state == before);
    }

    SECTION("non-add operation") {
        auto vector_state = astraea::graphics::ShaderVectorState{};
        vector_state.wave_size =
            astraea::graphics::ShaderWaveSize::wave32;
        const auto before = vector_state;

        const astraea::graphics::ShaderIrOperation operation =
            astraea::graphics::ShaderIrNop{
                .repeat_count = 1,
            };

        const auto result =
            astraea::graphics::
                execute_shader_vector_add_f32_exact_operation(
                    operation,
                    scalar_state,
                    vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderVectorExecutionErrorCode::
                    unsupported_operation);
        REQUIRE(vector_state == before);
    }
}
