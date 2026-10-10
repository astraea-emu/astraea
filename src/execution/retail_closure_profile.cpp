#include <astraea/execution/retail_closure_profile.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace astraea::execution {
namespace {

constexpr std::uint32_t kPtLoad = 1U;
constexpr std::uint32_t kPfExecute = 0x1U;

[[nodiscard]] bool overlaps_load_segment(
    astraea::memory::GuestRange candidate,
    const astraea::loader::ElfImage& elf) noexcept {
    constexpr std::uint32_t kPtLoadLocal = 1U;

    for (const auto& header : elf.program_headers) {
        if (header.type != kPtLoadLocal ||
            header.memory_size == 0U) {
            continue;
        }

        const auto range =
            astraea::memory::GuestRange::create(
                astraea::memory::GuestAddress{
                    header.virtual_address},
                astraea::memory::GuestSize{
                    header.memory_size});
        if (!range.has_value() ||
            candidate.overlaps(range.value())) {
            return true;
        }
    }
    return false;
}


[[nodiscard]] constexpr std::uint64_t saturating_add(
    std::uint64_t lhs,
    std::uint64_t rhs) noexcept {
    if (rhs >
        std::numeric_limits<std::uint64_t>::max() - lhs) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

[[nodiscard]] constexpr std::uint64_t saturating_size(
    std::size_t value) noexcept {
    if constexpr (
        sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return std::numeric_limits<std::uint64_t>::max();
        }
    }
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] constexpr std::uint64_t relocation_count(
    const std::optional<
        astraea::loader::DynamicRelocationTableDescriptor>& table) noexcept {
    return table.has_value() ? table->count : 0U;
}

[[nodiscard]] constexpr bool is_sce_dynamic_namespace(
    std::int64_t tag) noexcept {
    if (tag < 0) {
        return false;
    }
    const auto raw =
        static_cast<std::uint64_t>(tag);
    return (raw & 0xffff0000ULL) == 0x61000000ULL;
}

}  // namespace

std::optional<astraea::memory::GuestRange>
choose_retail_analysis_stack(
    std::span<const std::byte> artifact,
    astraea::loader::ElfParseProfile profile) noexcept {
    try {
        const auto parsed =
            astraea::loader::parse_elf64(artifact, profile);

        for (std::size_t index = 0U;
             index < kRetailAnalysisStackCandidateCount;
             ++index) {
            const auto delta =
                static_cast<std::uint64_t>(index) *
                kRetailAnalysisStackStride;
            if (delta > kRetailAnalysisStackFirstBase) {
                break;
            }

            const auto candidate =
                astraea::memory::GuestRange::create(
                    astraea::memory::GuestAddress{
                        kRetailAnalysisStackFirstBase - delta},
                    astraea::memory::GuestSize{
                        kRetailAnalysisStackSize});
            if (!candidate.has_value()) {
                continue;
            }

            // Preserve the eventual GuestImage parser error for malformed
            // input. Any syntactically valid candidate is sufficient until
            // build_guest_image() reports the real loader boundary.
            if (!parsed.has_value() ||
                !overlaps_load_segment(
                    candidate.value(),
                    parsed.value())) {
                return candidate.value();
            }
        }

        return std::nullopt;
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    } catch (const std::length_error&) {
        return std::nullopt;
    }
}

RetailStaticClosureArtifactResult
profile_retail_artifact(
    std::vector<std::byte> artifact_bytes,
    astraea::loader::ElfParseProfile profile) {
    try {
        const auto stack =
            choose_retail_analysis_stack(
                artifact_bytes, profile);
        if (!stack.has_value()) {
            return RetailStaticClosureArtifactResult::failure(
                RetailStaticClosureArtifactError{
                    .code =
                        RetailStaticClosureArtifactErrorCode::
                            planning_stack_unavailable,
                    .guest_image_error = std::nullopt,
                    .profile_error = std::nullopt,
                });
        }

        auto image =
            astraea::loader::build_guest_image(
                astraea::loader::GuestImageRequest{
                    .image_bytes =
                        std::move(artifact_bytes),
                    .initial_stack =
                        astraea::loader::InitialStackRequest{
                            .storage = stack.value(),
                            .arguments = {
                                "astraea-retail-profile",
                            },
                            .environment = {},
                            .auxiliary_vector = {},
                        },
                    .elf_profile = profile,
                });

        if (!image.has_value()) {
            return RetailStaticClosureArtifactResult::failure(
                RetailStaticClosureArtifactError{
                    .code =
                        RetailStaticClosureArtifactErrorCode::
                            guest_image_failure,
                    .guest_image_error =
                        std::move(image.error()),
                    .profile_error = std::nullopt,
                });
        }

        const auto profile =
            profile_retail_guest_image(
                image.value());
        if (!profile.has_value()) {
            return RetailStaticClosureArtifactResult::failure(
                RetailStaticClosureArtifactError{
                    .code =
                        RetailStaticClosureArtifactErrorCode::
                            profile_failure,
                    .guest_image_error = std::nullopt,
                    .profile_error = profile.error(),
                });
        }

        return RetailStaticClosureArtifactResult::success(
            profile.value());
    } catch (const std::bad_alloc&) {
        return RetailStaticClosureArtifactResult::failure(
            RetailStaticClosureArtifactError{
                .code =
                    RetailStaticClosureArtifactErrorCode::
                        host_allocation_failure,
                .guest_image_error = std::nullopt,
                .profile_error = std::nullopt,
            });
    } catch (const std::length_error&) {
        return RetailStaticClosureArtifactResult::failure(
            RetailStaticClosureArtifactError{
                .code =
                    RetailStaticClosureArtifactErrorCode::
                        host_allocation_failure,
                .guest_image_error = std::nullopt,
                .profile_error = std::nullopt,
            });
    }
}

RetailStaticClosureProfileResult
profile_retail_guest_image(
    const astraea::loader::GuestImage& image) noexcept {
    RetailStaticClosureProfile profile{};
    profile.program_header_count =
        saturating_size(image.elf.program_headers.size());

    for (const auto& header : image.elf.program_headers) {
        if (header.type != kPtLoad) {
            continue;
        }

        profile.load_segment_count =
            saturating_add(
                profile.load_segment_count,
                1U);
        profile.load_memory_bytes =
            saturating_add(
                profile.load_memory_bytes,
                header.memory_size);

        if ((header.flags & kPfExecute) != 0U) {
            profile.executable_load_segment_count =
                saturating_add(
                    profile.executable_load_segment_count,
                    1U);
            profile.executable_load_memory_bytes =
                saturating_add(
                    profile.executable_load_memory_bytes,
                    header.memory_size);
        }
    }

    if (image.dynamic_strings.has_value()) {
        profile.generic_needed_count =
            saturating_size(
                image.dynamic_strings->needed.size());
    }

    if (image.dynamic_table.has_value()) {
        const auto sce =
            astraea::loader::build_sce_dynamic_metadata(
                image.dynamic_table.value());
        if (!sce.has_value()) {
            return RetailStaticClosureProfileResult::failure(
                RetailStaticClosureProfileError{
                    .code =
                        RetailStaticClosureProfileErrorCode::
                            sce_dynamic_metadata_failure,
                    .sce_dynamic_metadata_error =
                        sce.error(),
                });
        }

        for (const auto& record : sce->records) {
            switch (record.kind) {
            case astraea::loader::SceDynamicTagKind::needed_module:
                profile.sce_needed_module_count =
                    saturating_add(
                        profile.sce_needed_module_count,
                        1U);
                break;
            case astraea::loader::SceDynamicTagKind::import_library:
                profile.sce_import_library_count =
                    saturating_add(
                        profile.sce_import_library_count,
                        1U);
                break;
            case astraea::loader::SceDynamicTagKind::unknown:
                if (is_sce_dynamic_namespace(
                        record.raw_tag)) {
                    profile.sce_unknown_dynamic_record_count =
                        saturating_add(
                            profile.sce_unknown_dynamic_record_count,
                            1U);
                }
                break;
            default:
                break;
            }
        }
    }

    if (image.dynamic_symbols.has_value()) {
        profile.dynamic_symbol_count =
            image.dynamic_symbols->symbol_count;
    }

    profile.rel_relocation_count =
        relocation_count(image.general_relocations.rel);
    profile.rela_relocation_count =
        relocation_count(image.general_relocations.rela);
    profile.plt_relocation_count =
        relocation_count(image.plt_relocations);
    profile.total_relocation_count =
        saturating_add(
            saturating_add(
                profile.rel_relocation_count,
                profile.rela_relocation_count),
            profile.plt_relocation_count);

    if (image.tls.has_value()) {
        profile.tls_present = true;
        profile.tls_initialized_bytes =
            image.tls->initialized_size.value();
        profile.tls_total_bytes =
            image.tls->total_size.value();
        profile.tls_alignment =
            image.tls->alignment;
    }

    return RetailStaticClosureProfileResult::success(profile);
}

}  // namespace astraea::execution
