#include <astraea/research/agc_link_shaders_tail_candidate.hpp>

namespace astraea::research {

AgcLinkShadersTailCandidateResult
make_agc_link_shaders_tail_candidate(
    const AgcLinkShadersTailCandidateInputs& inputs) noexcept {
    if (inputs.primitive_type !=
        astraea::execution::
            kSceAgcLinkShadersTriangleListPrimitiveType) {
        return AgcLinkShadersTailCandidateResult::failure(
            AgcLinkShadersTailCandidateError{
                .code =
                    AgcLinkShadersTailCandidateErrorCode::
                        unsupported_primitive_type,
                .primitive_type = inputs.primitive_type,
            });
    }

    using Basis = AgcLinkShadersTailCandidateBasis;
    using Record =
        astraea::execution::SceAgcLinkShadersRegisterRecord;

    return AgcLinkShadersTailCandidateResult::success(
        AgcLinkShadersTailCandidate{
            .context_record =
                AgcLinkShadersTailCandidateRecord{
                    .record =
                        Record{
                            .offset =
                                kAgcLinkShadersCandidateVgtShaderStagesEnOffset,
                            .value =
                                inputs.vgt_shader_stages_en_value,
                        },
                    .basis =
                        Basis::
                            corroborated_identity_supplied_pipeline_value,
                },
            .user_config_records =
                std::array<AgcLinkShadersTailCandidateRecord, 3>{
                    AgcLinkShadersTailCandidateRecord{
                        .record =
                            Record{
                                .offset =
                                    kAgcLinkShadersCandidateGeCntlOffset,
                                .value =
                                    inputs.ge_cntl_value,
                            },
                        .basis =
                            Basis::
                                corroborated_identity_supplied_pipeline_value,
                    },
                    AgcLinkShadersTailCandidateRecord{
                        .record =
                            Record{
                                .offset =
                                    kAgcLinkShadersCandidateGeUserVgprEnOffset,
                                .value =
                                    inputs.ge_user_vgpr_en_value,
                            },
                        .basis =
                            Basis::
                                corroborated_identity_supplied_pipeline_value,
                    },
                    AgcLinkShadersTailCandidateRecord{
                        .record =
                            Record{
                                .offset =
                                    kAgcLinkShadersCandidateVgtPrimitiveTypeOffset,
                                .value = inputs.primitive_type,
                            },
                        .basis =
                            Basis::
                                corroborated_identity_call_argument_value,
                    },
                },
        });
}

}  // namespace astraea::research
