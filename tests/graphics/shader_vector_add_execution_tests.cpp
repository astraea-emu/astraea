#include <astraea/graphics/shader_vector_add_execution.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::ShaderIrOperation add_operation(
    std::uint8_t destination,
    std::uint8_t source0,
    std::uint8_t source1) {
    return astraea::graphics::ShaderIrVectorAddF32{
        .destination =
            astraea::graphics::ShaderIrVgpr{
                .index = destination,
            },
        .source0 =
            astraea::graphics::ShaderIrVgpr{
                .index = source0,
            },
        .source1 =
            astraea::graphics::ShaderIrVgpr{
                .index = source1,
            },
    };
}

astraea::graphics::ShaderVectorState wave32_state() {
    astraea::graphics::ShaderVectorState state{};
    state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    return state;
}

astraea::graphics::ShaderVectorState wave64_state() {
    astraea::graphics::ShaderVectorState state{};
    state.wave_size =
        astraea::graphics::ShaderWaveSize::wave64;
    return state;
}

}  // namespace

TEST_CASE(
    "exact V_ADD_F32 executes mode-independent positive and negative normal sums",
    "[graphics][shader-execution][vector][add-f32][exact]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 0U) |
        (std::uint64_t{1} << 1U);
    auto vector_state = wave32_state();

    vector_state.vgprs[2][0] = 0x3f800000U;
    vector_state.vgprs[3][0] = 0x40000000U;
    vector_state.vgprs[2][1] = 0xc0000000U;
    vector_state.vgprs[3][1] = 0x3f000000U;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(1, 2, 3),
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->wave_size ==
        astraea::graphics::ShaderWaveSize::wave32);
    REQUIRE(result->destination_vgpr == 1);
    REQUIRE(result->source0_vgpr == 2);
    REQUIRE(result->source1_vgpr == 3);
    REQUIRE(
        result->active_lane_mask ==
        ((std::uint64_t{1} << 0U) |
         (std::uint64_t{1} << 1U)));
    REQUIRE(vector_state.vgprs[1][0] == 0x40400000U);
    REQUIRE(vector_state.vgprs[1][1] == 0xbfc00000U);
    REQUIRE(result->written_values[0] == 0x40400000U);
    REQUIRE(result->written_values[1] == 0xbfc00000U);
}

TEST_CASE(
    "exact V_ADD_F32 wave64 executes active high lane",
    "[graphics][shader-execution][vector][add-f32][wave64]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = std::uint64_t{1} << 40U;
    auto vector_state = wave64_state();

    vector_state.vgprs[5][40] = 0x3f000000U;
    vector_state.vgprs[6][40] = 0x3f000000U;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(4, 5, 6),
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->active_lane_mask ==
        (std::uint64_t{1} << 40U));
    REQUIRE(vector_state.vgprs[4][40] == 0x3f800000U);
    REQUIRE(result->written_values[40] == 0x3f800000U);
}

TEST_CASE(
    "exact V_ADD_F32 ignores inactive lanes and preserves destinations",
    "[graphics][shader-execution][vector][add-f32][exec]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = std::uint64_t{1} << 0U;
    auto vector_state = wave32_state();

    vector_state.vgprs[8][0] = 0x3f800000U;
    vector_state.vgprs[9][0] = 0x3f800000U;

    vector_state.vgprs[8][1] = 0x7fc00000U;
    vector_state.vgprs[9][1] = 0x00000001U;
    vector_state.vgprs[7][1] = 0xdeadbeefU;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(7, 8, 9),
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(vector_state.vgprs[7][0] == 0x40000000U);
    REQUIRE(vector_state.vgprs[7][1] == 0xdeadbeefU);
    REQUIRE(result->written_values[1] == 0U);
}

TEST_CASE(
    "exact V_ADD_F32 supports destination source aliasing atomically",
    "[graphics][shader-execution][vector][add-f32][alias]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 0U) |
        (std::uint64_t{1} << 1U);
    auto vector_state = wave32_state();

    vector_state.vgprs[10][0] = 0x3f800000U;
    vector_state.vgprs[10][1] = 0x40000000U;
    vector_state.vgprs[11][0] = 0x40000000U;
    vector_state.vgprs[11][1] = 0x3f800000U;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(10, 10, 11),
                scalar_state,
                vector_state);

    REQUIRE(result.has_value());
    REQUIRE(vector_state.vgprs[10][0] == 0x40400000U);
    REQUIRE(vector_state.vgprs[10][1] == 0x40400000U);
}

TEST_CASE(
    "exact V_ADD_F32 rejects rounding-sensitive inexact sum without mutation",
    "[graphics][shader-execution][vector][add-f32][unsupported]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();

    vector_state.vgprs[2][0] = 0x3f800000U;
    vector_state.vgprs[3][0] = 0x33800000U;
    vector_state.vgprs[1][0] = 0xaaaaaaaaU;
    const auto before = vector_state;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(1, 2, 3),
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderVectorAddF32ExecutionErrorCode::
                unsupported_f32_case);
    REQUIRE(
        result.error().lane_index ==
        std::optional<std::size_t>{0});
    REQUIRE(
        result.error().unsupported_reason ==
        std::optional<
            astraea::graphics::
                ShaderVectorAddF32UnsupportedReason>{
            astraea::graphics::
                ShaderVectorAddF32UnsupportedReason::
                    inexact_result});
    REQUIRE(vector_state == before);
}

TEST_CASE(
    "exact V_ADD_F32 rejects non-normal inputs without mutation",
    "[graphics][shader-execution][vector][add-f32][unsupported]") {
    const auto run =
        [](std::uint32_t source0_bits) {
            astraea::graphics::ShaderScalarState scalar_state{};
            scalar_state.exec = 1;
            auto vector_state = wave32_state();

            vector_state.vgprs[2][0] = source0_bits;
            vector_state.vgprs[3][0] = 0x3f800000U;
            vector_state.vgprs[1][0] = 0x12345678U;
            const auto before = vector_state;

            const auto result =
                astraea::graphics::
                    execute_shader_vector_add_f32_exact_operation(
                        add_operation(1, 2, 3),
                        scalar_state,
                        vector_state);

            REQUIRE_FALSE(result.has_value());
            REQUIRE(
                result.error().unsupported_reason ==
                std::optional<
                    astraea::graphics::
                        ShaderVectorAddF32UnsupportedReason>{
                    astraea::graphics::
                        ShaderVectorAddF32UnsupportedReason::
                            non_normal_input});
            REQUIRE(vector_state == before);
        };

    SECTION("subnormal") {
        run(0x00000001U);
    }

    SECTION("infinity") {
        run(0x7f800000U);
    }

    SECTION("NaN") {
        run(0x7fc12345U);
    }

    SECTION("zero") {
        run(0x00000000U);
    }
}

TEST_CASE(
    "exact V_ADD_F32 rejects exact cancellation to zero",
    "[graphics][shader-execution][vector][add-f32][unsupported]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();

    vector_state.vgprs[2][0] = 0x3f800000U;
    vector_state.vgprs[3][0] = 0xbf800000U;
    vector_state.vgprs[1][0] = 0xabcdef01U;
    const auto before = vector_state;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(1, 2, 3),
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().unsupported_reason ==
        std::optional<
            astraea::graphics::
                ShaderVectorAddF32UnsupportedReason>{
            astraea::graphics::
                ShaderVectorAddF32UnsupportedReason::
                    zero_result});
    REQUIRE(vector_state == before);
}

TEST_CASE(
    "exact V_ADD_F32 rejects overflow outside finite normal range",
    "[graphics][shader-execution][vector][add-f32][unsupported]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 1;
    auto vector_state = wave32_state();

    vector_state.vgprs[2][0] = 0x7f7fffffU;
    vector_state.vgprs[3][0] = 0x7f7fffffU;
    vector_state.vgprs[1][0] = 0x11111111U;
    const auto before = vector_state;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(1, 2, 3),
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().unsupported_reason ==
        std::optional<
            astraea::graphics::
                ShaderVectorAddF32UnsupportedReason>{
            astraea::graphics::
                ShaderVectorAddF32UnsupportedReason::
                    non_normal_result});
    REQUIRE(vector_state == before);
}

TEST_CASE(
    "exact V_ADD_F32 validates every active lane before any write",
    "[graphics][shader-execution][vector][add-f32][atomic]") {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 0U) |
        (std::uint64_t{1} << 1U);
    auto vector_state = wave32_state();

    vector_state.vgprs[2][0] = 0x3f800000U;
    vector_state.vgprs[3][0] = 0x40000000U;
    vector_state.vgprs[2][1] = 0x3f800000U;
    vector_state.vgprs[3][1] = 0x33800000U;
    vector_state.vgprs[1][0] = 0xaaaaaaaaU;
    vector_state.vgprs[1][1] = 0xbbbbbbbbU;
    const auto before = vector_state;

    const auto result =
        astraea::graphics::
            execute_shader_vector_add_f32_exact_operation(
                add_operation(1, 2, 3),
                scalar_state,
                vector_state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().lane_index ==
        std::optional<std::size_t>{1});
    REQUIRE(vector_state == before);
}

TEST_CASE(
    "exact V_ADD_F32 rejects unspecified wave size and non-add IR",
    "[graphics][shader-execution][vector][add-f32][validation]") {
    SECTION("unspecified wave size") {
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        astraea::graphics::ShaderVectorState vector_state{};
        vector_state.vgprs[2][0] = 0x3f800000U;
        vector_state.vgprs[3][0] = 0x40000000U;
        const auto before = vector_state;

        const auto result =
            astraea::graphics::
                execute_shader_vector_add_f32_exact_operation(
                    add_operation(1, 2, 3),
                    scalar_state,
                    vector_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                ShaderVectorAddF32ExecutionErrorCode::
                    invalid_wave_size);
        REQUIRE(vector_state == before);
    }

    SECTION("unsupported operation") {
        astraea::graphics::ShaderScalarState scalar_state{};
        scalar_state.exec = 1;
        auto vector_state = wave32_state();
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
                ShaderVectorAddF32ExecutionErrorCode::
                    unsupported_operation);
        REQUIRE(vector_state == before);
    }
}
