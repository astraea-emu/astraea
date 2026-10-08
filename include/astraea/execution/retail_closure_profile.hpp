#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint64_t
    kRetailAnalysisStackSize = 2ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t
    kRetailAnalysisStackFirstBase = 0x00007fff00000000ULL;
inline constexpr std::uint64_t
    kRetailAnalysisStackStride = 0x01000000ULL;
inline constexpr std::size_t
    kRetailAnalysisStackCandidateCount = 256U;

[[nodiscard]] std::optional<astraea::memory::GuestRange>
choose_retail_analysis_stack(
    std::span<const std::byte> artifact) noexcept;

struct RetailStaticClosureProfile {
    std::uint64_t program_header_count = 0;
    std::uint64_t load_segment_count = 0;
    std::uint64_t load_memory_bytes = 0;
    std::uint64_t executable_load_segment_count = 0;
    std::uint64_t executable_load_memory_bytes = 0;

    std::uint64_t generic_needed_count = 0;
    std::uint64_t sce_needed_module_count = 0;
    std::uint64_t sce_import_library_count = 0;
    std::uint64_t sce_unknown_dynamic_record_count = 0;

    std::optional<std::uint64_t> dynamic_symbol_count;

    std::uint64_t rel_relocation_count = 0;
    std::uint64_t rela_relocation_count = 0;
    std::uint64_t plt_relocation_count = 0;
    std::uint64_t total_relocation_count = 0;

    bool tls_present = false;
    std::uint64_t tls_initialized_bytes = 0;
    std::uint64_t tls_total_bytes = 0;
    std::uint64_t tls_alignment = 0;

    auto operator<=>(const RetailStaticClosureProfile&) const = default;
};

enum class RetailStaticClosureProfileErrorCode {
    sce_dynamic_metadata_failure,
};

struct RetailStaticClosureProfileError {
    RetailStaticClosureProfileErrorCode code =
        RetailStaticClosureProfileErrorCode::
            sce_dynamic_metadata_failure;
    std::optional<astraea::loader::SceDynamicMetadataError>
        sce_dynamic_metadata_error;

    auto operator<=>(const RetailStaticClosureProfileError&) const = default;
};

using RetailStaticClosureProfileResult =
    astraea::core::Result<
        RetailStaticClosureProfile,
        RetailStaticClosureProfileError>;

enum class RetailStaticClosureArtifactErrorCode {
    planning_stack_unavailable,
    guest_image_failure,
    profile_failure,
    host_allocation_failure,
};

struct RetailStaticClosureArtifactError {
    RetailStaticClosureArtifactErrorCode code =
        RetailStaticClosureArtifactErrorCode::
            planning_stack_unavailable;
    std::optional<astraea::loader::GuestImageError>
        guest_image_error;
    std::optional<RetailStaticClosureProfileError>
        profile_error;
};

using RetailStaticClosureArtifactResult =
    astraea::core::Result<
        RetailStaticClosureProfile,
        RetailStaticClosureArtifactError>;

// Builds a validated PS5/SCE GuestImage only for structural analysis and then
// returns the same data-only closure profile. No guest instruction executes.
[[nodiscard]] RetailStaticClosureArtifactResult
profile_retail_artifact(
    std::vector<std::byte> artifact_bytes);

// Produces a data-only structural pressure profile from an already validated
// GuestImage. The profile is intended for comparing candidate workloads and
// must not be interpreted as a compatibility percentage or an execution
// admission decision.
[[nodiscard]] RetailStaticClosureProfileResult
profile_retail_guest_image(
    const astraea::loader::GuestImage& image) noexcept;

}  // namespace astraea::execution
