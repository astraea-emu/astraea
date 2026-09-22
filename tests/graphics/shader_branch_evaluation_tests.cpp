#include <astraea/graphics/shader_branch_evaluation.hpp>

#include <cstdint>
#include <optional>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::ShaderIrOperation conditional_branch(
    astraea::graphics::ShaderIrBranchCondition condition,
    std::int32_t byte_delta = 12) {
    return astraea::graphics::ShaderIrConditionalRelativeBranch{
        .condition = condition,
        .byte_delta = byte_delta,
    };
}

}  // namespace

TEST_CASE(
    "unconditional Shader IR branch is always taken",
    "[graphics][shader-execution][branch]") {
    astraea::graphics::ShaderScalarState state{};
    state.scc = false;
    state.vcc = 0;
    state.exec = 0;
    const auto before = state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrRelativeBranch{
            .byte_delta = -8,
        };

    const auto result =
        astraea::graphics::
            evaluate_shader_branch_operation(
                operation,
                state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::ShaderBranchDecisionKind::
            unconditional);
    REQUIRE_FALSE(result->condition.has_value());
    REQUIRE(result->byte_delta == -8);
    REQUIRE(result->taken);
    REQUIRE(state == before);
}

TEST_CASE(
    "SCC branch conditions use explicit scalar state",
    "[graphics][shader-execution][branch][scc]") {
    SECTION("SCC0") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = false;

        auto result =
            astraea::graphics::
                evaluate_shader_branch_operation(
                    conditional_branch(
                        astraea::graphics::
                            ShaderIrBranchCondition::
                                scc_zero),
                    state);

        REQUIRE(result.has_value());
        REQUIRE(result->taken);

        state.scc = true;
        result =
            astraea::graphics::
                evaluate_shader_branch_operation(
                    conditional_branch(
                        astraea::graphics::
                            ShaderIrBranchCondition::
                                scc_zero),
                    state);
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->taken);
    }

    SECTION("SCC1") {
        astraea::graphics::ShaderScalarState state{};
        state.scc = true;

        auto result =
            astraea::graphics::
                evaluate_shader_branch_operation(
                    conditional_branch(
                        astraea::graphics::
                            ShaderIrBranchCondition::
                                scc_one),
                    state);

        REQUIRE(result.has_value());
        REQUIRE(result->taken);

        state.scc = false;
        result =
            astraea::graphics::
                evaluate_shader_branch_operation(
                    conditional_branch(
                        astraea::graphics::
                            ShaderIrBranchCondition::
                                scc_one),
                    state);
        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->taken);
    }
}

TEST_CASE(
    "VCC branch conditions test the entire explicit mask",
    "[graphics][shader-execution][branch][vcc]") {
    astraea::graphics::ShaderScalarState state{};

    state.vcc = 0;
    auto zero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            vcc_zero),
                state);
    auto nonzero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            vcc_nonzero),
                state);

    REQUIRE(zero.has_value());
    REQUIRE(zero->taken);
    REQUIRE(nonzero.has_value());
    REQUIRE_FALSE(nonzero->taken);

    state.vcc = 0x8000000000000000ULL;
    zero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            vcc_zero),
                state);
    nonzero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            vcc_nonzero),
                state);

    REQUIRE(zero.has_value());
    REQUIRE_FALSE(zero->taken);
    REQUIRE(nonzero.has_value());
    REQUIRE(nonzero->taken);
}

TEST_CASE(
    "EXEC branch conditions test the entire explicit mask",
    "[graphics][shader-execution][branch][exec]") {
    astraea::graphics::ShaderScalarState state{};

    state.exec = 0;
    auto zero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            exec_zero),
                state);
    auto nonzero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            exec_nonzero),
                state);

    REQUIRE(zero.has_value());
    REQUIRE(zero->taken);
    REQUIRE(nonzero.has_value());
    REQUIRE_FALSE(nonzero->taken);

    state.exec = 1;
    zero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            exec_zero),
                state);
    nonzero =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::
                            exec_nonzero),
                state);

    REQUIRE(zero.has_value());
    REQUIRE_FALSE(zero->taken);
    REQUIRE(nonzero.has_value());
    REQUIRE(nonzero->taken);
}

TEST_CASE(
    "conditional branch decision preserves condition and byte delta",
    "[graphics][shader-execution][branch]") {
    astraea::graphics::ShaderScalarState state{};
    state.scc = true;
    const auto before = state;

    const auto result =
        astraea::graphics::
            evaluate_shader_branch_operation(
                conditional_branch(
                    astraea::graphics::
                        ShaderIrBranchCondition::scc_one,
                    -20),
                state);

    REQUIRE(result.has_value());
    REQUIRE(
        result->kind ==
        astraea::graphics::ShaderBranchDecisionKind::
            conditional);
    REQUIRE(
        result->condition ==
        std::optional<
            astraea::graphics::ShaderIrBranchCondition>{
                astraea::graphics::
                    ShaderIrBranchCondition::scc_one});
    REQUIRE(result->byte_delta == -20);
    REQUIRE(result->taken);
    REQUIRE(state == before);
}

TEST_CASE(
    "invalid direct-constructed branch condition is rejected",
    "[graphics][shader-execution][branch][validation]") {
    astraea::graphics::ShaderScalarState state{};
    const auto before = state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::
            ShaderIrConditionalRelativeBranch{
                .condition =
                    static_cast<
                        astraea::graphics::
                            ShaderIrBranchCondition>(
                        0xff),
                .byte_delta = 4,
            };

    const auto result =
        astraea::graphics::
            evaluate_shader_branch_operation(
                operation,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderBranchEvaluationErrorCode::
                invalid_condition);
    REQUIRE(state == before);
}

TEST_CASE(
    "non-branch Shader IR operation is rejected",
    "[graphics][shader-execution][branch][unsupported]") {
    astraea::graphics::ShaderScalarState state{};
    state.scc = true;
    state.vcc = 3;
    state.exec = 5;
    const auto before = state;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrEndProgram{};

    const auto result =
        astraea::graphics::
            evaluate_shader_branch_operation(
                operation,
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderBranchEvaluationErrorCode::
                unsupported_operation);
    REQUIRE(state == before);
}
