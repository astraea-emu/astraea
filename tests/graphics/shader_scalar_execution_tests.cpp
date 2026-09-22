#include <astraea/graphics/shader_scalar_execution.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::ShaderScalarState seeded_state() {
    astraea::graphics::ShaderScalarState state{};
    state.vcc = 0x1122334455667788ULL;
    state.m0 = 0xa5a5f00dU;
    state.exec = 0x99aabbccddeeff00ULL;
    state.scc = true;
    return state;
}

}  // namespace

TEST_CASE(
    "scalar SGPR move executes without changing special scalar state",
    "[graphics][shader-execution][scalar][mov32]") {
    auto state = seeded_state();
    state.sgprs[4] = 0xdeadbeefU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove32{
            .destination =
                astraea::graphics::ShaderIrSgpr{
                    .index = 7,
                },
            .source =
                astraea::graphics::ShaderIrSgpr{
                    .index = 4,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->width ==
        astraea::graphics::ShaderScalarWriteWidth::
            bits32);
    REQUIRE(result->first_destination_sgpr == 7);
    REQUIRE(result->written_values[0] == 0xdeadbeefU);
    REQUIRE(result->written_values[1] == 0U);
    REQUIRE(state.sgprs[7] == 0xdeadbeefU);
    REQUIRE(state.vcc == 0x1122334455667788ULL);
    REQUIRE(state.m0 == 0xa5a5f00dU);
    REQUIRE(state.exec == 0x99aabbccddeeff00ULL);
    REQUIRE(state.scc);
}

TEST_CASE(
    "scalar inline integer and literal moves preserve exact bits",
    "[graphics][shader-execution][scalar][mov32]") {
    SECTION("negative inline integer") {
        auto state = seeded_state();
        const astraea::graphics::ShaderIrOperation operation =
            astraea::graphics::ShaderIrScalarMove32{
                .destination =
                    astraea::graphics::ShaderIrSgpr{
                        .index = 8,
                    },
                .source =
                    astraea::graphics::
                        ShaderIrInlineInteger32{
                            .value = -16,
                        },
            };

        const auto result =
            astraea::graphics::
                execute_shader_scalar_operation(
                    operation,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(state.sgprs[8] == 0xfffffff0U);
        REQUIRE(
            result->written_values[0] ==
            0xfffffff0U);
        REQUIRE(state.scc);
    }

    SECTION("literal bits") {
        auto state = seeded_state();
        const astraea::graphics::ShaderIrOperation operation =
            astraea::graphics::ShaderIrScalarMove32{
                .destination =
                    astraea::graphics::ShaderIrSgpr{
                        .index = 9,
                    },
                .source =
                    astraea::graphics::ShaderIrLiteral32{
                        .bits = 0x7fc12345U,
                    },
            };

        const auto result =
            astraea::graphics::
                execute_shader_scalar_operation(
                    operation,
                    state);

        REQUIRE(result.has_value());
        REQUIRE(state.sgprs[9] == 0x7fc12345U);
        REQUIRE(
            result->written_values[0] ==
            0x7fc12345U);
        REQUIRE(state.scc);
    }
}

TEST_CASE(
    "scalar special sources read explicit RDNA2 state",
    "[graphics][shader-execution][scalar][special]") {
    auto run =
        [](
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32 kind,
            std::uint32_t expected) {
            auto state = seeded_state();
            const astraea::graphics::ShaderIrOperation
                operation =
                    astraea::graphics::
                        ShaderIrScalarMove32{
                            .destination =
                                astraea::graphics::
                                    ShaderIrSgpr{
                                        .index = 10,
                                    },
                            .source =
                                astraea::graphics::
                                    ShaderIrSpecialScalarSource32{
                                        .kind = kind,
                                    },
                        };

            const auto result =
                astraea::graphics::
                    execute_shader_scalar_operation(
                        operation,
                        state);

            REQUIRE(result.has_value());
            REQUIRE(state.sgprs[10] == expected);
            REQUIRE(
                result->written_values[0] ==
                expected);
            REQUIRE(
                state.vcc ==
                0x1122334455667788ULL);
            REQUIRE(state.m0 == 0xa5a5f00dU);
            REQUIRE(
                state.exec ==
                0x99aabbccddeeff00ULL);
            REQUIRE(state.scc);
        };

    SECTION("VCC_LO") {
        run(
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32::
                    vcc_lo,
            0x55667788U);
    }
    SECTION("VCC_HI") {
        run(
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32::
                    vcc_hi,
            0x11223344U);
    }
    SECTION("M0") {
        run(
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32::m0,
            0xa5a5f00dU);
    }
    SECTION("NULL") {
        run(
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32::
                    null_register,
            0U);
    }
    SECTION("EXEC_LO") {
        run(
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32::
                    exec_lo,
            0xddeeff00U);
    }
    SECTION("EXEC_HI") {
        run(
            astraea::graphics::
                ShaderIrSpecialScalarSourceKind32::
                    exec_hi,
            0x99aabbccU);
    }
}

TEST_CASE(
    "scalar 64-bit SGPR pair move captures both source dwords",
    "[graphics][shader-execution][scalar][mov64]") {
    auto state = seeded_state();
    state.sgprs[2] = 0x01234567U;
    state.sgprs[3] = 0x89abcdefU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove64{
            .destination =
                astraea::graphics::ShaderIrSgprPair{
                    .first_index = 20,
                },
            .source =
                astraea::graphics::ShaderIrSgprPair{
                    .first_index = 2,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->width ==
        astraea::graphics::ShaderScalarWriteWidth::
            bits64);
    REQUIRE(result->first_destination_sgpr == 20);
    REQUIRE(result->written_values[0] == 0x01234567U);
    REQUIRE(result->written_values[1] == 0x89abcdefU);
    REQUIRE(state.sgprs[20] == 0x01234567U);
    REQUIRE(state.sgprs[21] == 0x89abcdefU);
    REQUIRE(state.scc);
}

TEST_CASE(
    "scalar move rejects invalid SGPR destination atomically",
    "[graphics][shader-execution][scalar][validation]") {
    auto state = seeded_state();
    state.sgprs[0] = 0x12345678U;
    const auto before = state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove32{
            .destination =
                astraea::graphics::ShaderIrSgpr{
                    .index = 106,
                },
            .source =
                astraea::graphics::ShaderIrSgpr{
                    .index = 0,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarExecutionErrorCode::
                invalid_sgpr_index);
    REQUIRE(
        result.error().role ==
        astraea::graphics::
            ShaderScalarExecutionOperandRole::
                destination);
    REQUIRE(result.error().sgpr_index == 106);
    REQUIRE(state == before);
}

TEST_CASE(
    "scalar move rejects invalid SGPR source atomically",
    "[graphics][shader-execution][scalar][validation]") {
    auto state = seeded_state();
    const auto before = state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove32{
            .destination =
                astraea::graphics::ShaderIrSgpr{
                    .index = 0,
                },
            .source =
                astraea::graphics::ShaderIrSgpr{
                    .index = 106,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarExecutionErrorCode::
                invalid_sgpr_index);
    REQUIRE(
        result.error().role ==
        astraea::graphics::
            ShaderScalarExecutionOperandRole::source);
    REQUIRE(result.error().sgpr_index == 106);
    REQUIRE(state == before);
}

TEST_CASE(
    "scalar move rejects malformed SGPR pair atomically",
    "[graphics][shader-execution][scalar][validation]") {
    auto state = seeded_state();
    state.sgprs[2] = 1U;
    state.sgprs[3] = 2U;
    const auto before = state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove64{
            .destination =
                astraea::graphics::ShaderIrSgprPair{
                    .first_index = 3,
                },
            .source =
                astraea::graphics::ShaderIrSgprPair{
                    .first_index = 2,
                },
        };

    const auto result =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarExecutionErrorCode::
                invalid_sgpr_pair);
    REQUIRE(
        result.error().role ==
        astraea::graphics::
            ShaderScalarExecutionOperandRole::
                destination);
    REQUIRE(result.error().sgpr_index == 3);
    REQUIRE(state == before);
}

TEST_CASE(
    "unsupported Shader IR operation does not mutate scalar state",
    "[graphics][shader-execution][scalar][unsupported]") {
    auto state = seeded_state();
    state.sgprs[1] = 0xabcdef01U;
    const auto before = state;

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
            execute_shader_scalar_operation(
                operation,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderScalarExecutionErrorCode::
                unsupported_operation);
    REQUIRE(
        result.error().role ==
        astraea::graphics::
            ShaderScalarExecutionOperandRole::none);
    REQUIRE(state == before);
}
