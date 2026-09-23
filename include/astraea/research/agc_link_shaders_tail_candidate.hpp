#pragma once

#include <array>
#include <compare>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/execution/sce_agc_link_shaders.hpp>

namespace astraea::research {

// These identities are corroborated by public linked-output layouts and
// independent register maps, but the corresponding values remain hypotheses
// until the reference-hardware observation required by issue #191 exists.
inline constexpr std::uint32_t
    kAgcLinkShadersCandidateVgtShaderStagesEnOffset = 0x2d5U;
inline constexpr std::uint32_t
    kAgcLinkShadersCandidateGeCntlOffset = 0x25bU;
inline constexpr std::uint32_t
    kAgcLinkShadersCandidateGeUserVgprEnOffset = 0x262U;
inline constexpr std::uint32_t
    kAgcLinkShadersCandidateVgtPrimitiveTypeOffset = 0x242U;

enum class AgcLinkShadersTailCandidateBasis {
    // The output register identity is independently corroborated, while the
    // value supplied here is a research input and is not native output.
    corroborated_identity_supplied_pipeline_value,

    // Primitive type is taken from the bounded LinkShaders call profile. The
    // hypothesis is that native LinkShaders returns it as VGT_PRIMITIVE_TYPE.
    corroborated_identity_call_argument_value,
};

struct AgcLinkShadersTailCandidateInputs {
    // Values are deliberately supplied by the research caller. This module
    // does not parse or trust an AGC shader "specials" ABI.
    std::uint32_t vgt_shader_stages_en_value = 0;
    std::uint32_t ge_cntl_value = 0;
    std::uint32_t ge_user_vgpr_en_value = 0;
    std::uint32_t primitive_type = 0;

    auto operator<=>(
        const AgcLinkShadersTailCandidateInputs&) const = default;
};

struct AgcLinkShadersTailCandidateRecord {
    astraea::execution::SceAgcLinkShadersRegisterRecord record;
    AgcLinkShadersTailCandidateBasis basis =
        AgcLinkShadersTailCandidateBasis::
            corroborated_identity_supplied_pipeline_value;

    auto operator<=>(
        const AgcLinkShadersTailCandidateRecord&) const = default;
};

struct AgcLinkShadersTailCandidate {
    // Candidate for the one currently unknown context record, CX[32].
    AgcLinkShadersTailCandidateRecord context_record;

    // Candidates for the three currently unknown user-config records.
    std::array<AgcLinkShadersTailCandidateRecord, 3>
        user_config_records{};

    auto operator<=>(
        const AgcLinkShadersTailCandidate&) const = default;
};

enum class AgcLinkShadersTailCandidateErrorCode {
    unsupported_primitive_type,
};

struct AgcLinkShadersTailCandidateError {
    AgcLinkShadersTailCandidateErrorCode code =
        AgcLinkShadersTailCandidateErrorCode::
            unsupported_primitive_type;
    std::uint32_t primitive_type = 0;

    auto operator<=>(
        const AgcLinkShadersTailCandidateError&) const = default;
};

using AgcLinkShadersTailCandidateResult =
    astraea::core::Result<
        AgcLinkShadersTailCandidate,
        AgcLinkShadersTailCandidateError>;

// Builds an explicitly non-authoritative candidate for the four unknown
// LinkShaders tail records. This is a research helper only:
//
// - it performs no guest-memory access;
// - it performs no shader parsing;
// - it performs no HLE dispatch;
// - it does not claim that native LinkShaders wrote these records.
//
// Only the exact primitive-4 profile already bounded by the production
// LinkShaders planner is accepted. Native evidence remains the sole promotion
// path into guest-visible behavior.
[[nodiscard]] AgcLinkShadersTailCandidateResult
make_agc_link_shaders_tail_candidate(
    const AgcLinkShadersTailCandidateInputs& inputs) noexcept;

}  // namespace astraea::research
