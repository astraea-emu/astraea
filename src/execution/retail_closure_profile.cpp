#include <astraea/execution/retail_closure_profile.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace astraea::execution {
namespace {

constexpr std::uint32_t kPtLoad = 1U;
constexpr std::uint32_t kPfExecute = 0x1U;

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
