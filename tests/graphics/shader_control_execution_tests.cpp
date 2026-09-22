#include <astraea/graphics/shader_control_execution.hpp>

#include <catch2/catch_test_macros.hpp>

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
