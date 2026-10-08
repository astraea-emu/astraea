#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::execution {

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

// Produces a data-only structural pressure profile from an already validated
// GuestImage. The profile is intended for comparing candidate workloads and
// must not be interpreted as a compatibility percentage or an execution
// admission decision.
[[nodiscard]] RetailStaticClosureProfileResult
profile_retail_guest_image(
    const astraea::loader::GuestImage& image) noexcept;

// Builds a validated PS5/SCE GuestImage using an analysis-only synthetic
// non-overlapping stack and returns the static closure dimensions. No guest
// instruction is executed.
enum class RetailArtifactClosureProfileErrorCode {
    analysis_stack_unavailable,
    guest_image_failure,
    static_profile_failure,
    host_allocation_failure,
};

struct RetailArtifactClosureProfileError {
    RetailArtifactClosureProfileErrorCode code =
        RetailArtifactClosureProfileErrorCode::guest_image_failure;
    std::optional<astraea::loader::GuestImageError> guest_image_error;
    std::optional<RetailStaticClosureProfileError> static_profile_error;

    auto operator<=>(const RetailArtifactClosureProfileError&) const = default;
};

using RetailArtifactClosureProfileResult =
    astraea::core::Result<
        RetailStaticClosureProfile,
        RetailArtifactClosureProfileError>;

[[nodiscard]] RetailArtifactClosureProfileResult
profile_retail_artifact(
    std::vector<std::byte> artifact_bytes);

}  // namespace astraea::execution
