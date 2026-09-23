#include <astraea/research/agc_link_shaders_tail_candidate.hpp>

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::SceAgcLinkShadersRegisterRecord;
using astraea::research::AgcLinkShadersTailCandidateBasis;
using astraea::research::AgcLinkShadersTailCandidateErrorCode;
using astraea::research::AgcLinkShadersTailCandidateInputs;

}  // namespace

TEST_CASE(
    "LinkShaders tail candidate preserves one hardware-valid supplied pipeline",
    "[research][agc][link-shaders][candidate]") {
    const auto result =
        astraea::research::
            make_agc_link_shaders_tail_candidate(
                AgcLinkShadersTailCandidateInputs{
                    .vgt_shader_stages_en_value =
                        0x02002000U,
                    .ge_cntl_value = 0x00008040U,
                    .ge_user_vgpr_en_value = 0U,
                    .primitive_type = 4U,
                });

    REQUIRE(result.has_value());

    REQUIRE(
        result->context_record.record ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x2d5U,
            .value = 0x02002000U,
        });
    REQUIRE(
        result->context_record.basis ==
        AgcLinkShadersTailCandidateBasis::
            corroborated_identity_supplied_pipeline_value);

    REQUIRE(
        result->user_config_records[0].record ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x25bU,
            .value = 0x00008040U,
        });
    REQUIRE(
        result->user_config_records[1].record ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x262U,
            .value = 0U,
        });
    REQUIRE(
        result->user_config_records[2].record ==
        SceAgcLinkShadersRegisterRecord{
            .offset = 0x242U,
            .value = 4U,
        });
    REQUIRE(
        result->user_config_records[2].basis ==
        AgcLinkShadersTailCandidateBasis::
            corroborated_identity_call_argument_value);
}

TEST_CASE(
    "LinkShaders tail candidate does not collapse pipeline-dependent values into constants",
    "[research][agc][link-shaders][candidate][pipeline-dependent]") {
    const auto first =
        astraea::research::
            make_agc_link_shaders_tail_candidate(
                AgcLinkShadersTailCandidateInputs{
                    .vgt_shader_stages_en_value =
                        0x02002000U,
                    .ge_cntl_value = 0x00008040U,
                    .ge_user_vgpr_en_value = 0U,
                    .primitive_type = 4U,
                });
    const auto second =
        astraea::research::
            make_agc_link_shaders_tail_candidate(
                AgcLinkShadersTailCandidateInputs{
                    .vgt_shader_stages_en_value =
                        0x02412010U,
                    .ge_cntl_value = 0x0000fc80U,
                    .ge_user_vgpr_en_value = 0U,
                    .primitive_type = 4U,
                });

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(
        first->context_record.record.offset ==
        second->context_record.record.offset);
    REQUIRE(
        first->context_record.record.value !=
        second->context_record.record.value);

    REQUIRE(
        first->user_config_records[0].record.offset ==
        second->user_config_records[0].record.offset);
    REQUIRE(
        first->user_config_records[0].record.value !=
        second->user_config_records[0].record.value);

    REQUIRE(
        first->user_config_records[1] ==
        second->user_config_records[1]);
    REQUIRE(
        first->user_config_records[2] ==
        second->user_config_records[2]);
}

TEST_CASE(
    "LinkShaders tail candidate refuses to generalize beyond primitive four",
    "[research][agc][link-shaders][candidate][negative]") {
    const auto result =
        astraea::research::
            make_agc_link_shaders_tail_candidate(
                AgcLinkShadersTailCandidateInputs{
                    .vgt_shader_stages_en_value = 1U,
                    .ge_cntl_value = 2U,
                    .ge_user_vgpr_en_value = 3U,
                    .primitive_type = 6U,
                });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        AgcLinkShadersTailCandidateErrorCode::
            unsupported_primitive_type);
    REQUIRE(result.error().primitive_type == 6U);
}
