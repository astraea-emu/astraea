#include <astraea/loader/guest_image.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::loader {
namespace {

constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;

[[nodiscard]] GuestImageError image_error(
    GuestImageErrorCode code,
    GuestImageErrorCause cause = std::monostate{},
    std::optional<std::size_t> source_index = std::nullopt,
    std::optional<std::size_t> conflicting_index = std::nullopt) {
    return GuestImageError{
        .code = code,
        .cause = std::move(cause),
        .source_program_header_index = source_index,
        .conflicting_program_header_index = conflicting_index,
    };
}

[[nodiscard]] bool checked_add(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::uint64_t& result) noexcept {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool initialized_overlap_conflicts(
    const astraea::memory::MappingIntent& lhs,
    const astraea::memory::MappingIntent& rhs) noexcept {
    if (!lhs.range.overlaps(rhs.range)) {
        return false;
    }

    using astraea::memory::MappingBackingKind;

    if (lhs.backing.kind == MappingBackingKind::anonymous ||
        rhs.backing.kind == MappingBackingKind::anonymous) {
        return true;
    }

    if (lhs.backing.kind != rhs.backing.kind) {
        return true;
    }

    if (lhs.backing.kind == MappingBackingKind::zero_fill) {
        return false;
    }

    const auto overlap_base =
        std::max(lhs.range.base().value(), rhs.range.base().value());
    const auto lhs_delta = overlap_base - lhs.range.base().value();
    const auto rhs_delta = overlap_base - rhs.range.base().value();

    std::uint64_t lhs_source = 0;
    std::uint64_t rhs_source = 0;
    if (!checked_add(lhs.backing.file_offset, lhs_delta, lhs_source) ||
        !checked_add(rhs.backing.file_offset, rhs_delta, rhs_source)) {
        return true;
    }

    return lhs_source != rhs_source;
}

[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
first_mapping_conflict(
    const std::vector<astraea::memory::MappingIntent>& mappings) noexcept {
    for (std::size_t i = 0; i < mappings.size(); ++i) {
        for (std::size_t j = i + 1; j < mappings.size(); ++j) {
            if (!mappings[i].range.contains(mappings[j].range.base())) {
                break;
            }

            if (initialized_overlap_conflicts(mappings[i], mappings[j])) {
                return std::pair{
                    mappings[i].source_index,
                    mappings[j].source_index,
                };
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> first_stack_overlap(
    const astraea::memory::GuestRange& stack_storage,
    const std::vector<astraea::memory::MappingIntent>& mappings) noexcept {
    for (const auto& mapping : mappings) {
        if (stack_storage.overlaps(mapping.range)) {
            return mapping.source_index;
        }
    }
    return std::nullopt;
}

}  // namespace

astraea::memory::InitializedImageView::CreateResult
GuestImage::initialized_image_view() const {
    return astraea::memory::InitializedImageView::create(
        image_bytes,
        mappings);
}

astraea::core::Result<
    std::optional<std::vector<std::byte>>,
    TlsTemplateError>
GuestImage::materialize_tls() const {
    using Result = astraea::core::Result<
        std::optional<std::vector<std::byte>>,
        TlsTemplateError>;

    if (!tls.has_value()) {
        return Result::success(std::nullopt);
    }

    auto materialized = materialize_tls_template(*tls, image_bytes);
    if (!materialized.has_value()) {
        return Result::failure(materialized.error());
    }

    return Result::success(
        std::optional<std::vector<std::byte>>{
            std::move(materialized.value())});
}

[[nodiscard]] GuestImageResult build_guest_image_impl(GuestImageRequest request) {
    auto parsed = parse_elf64(request.image_bytes);
    if (!parsed.has_value()) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::elf_parse_failure,
                parsed.error()));
    }

    ElfImage elf = std::move(parsed.value());

    std::vector<astraea::memory::MappingIntent> mappings;
    for (const auto& header : elf.program_headers) {
        if (header.type != kPtLoad) {
            continue;
        }

        auto intents = astraea::memory::mapping_intents_from_load(header);
        if (!intents.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::mapping_failure,
                    intents.error(),
                    intents.error().first_source_index,
                    intents.error().second_source_index));
        }

        mappings.insert(
            mappings.end(),
            intents->begin(),
            intents->end());
    }

    std::sort(
        mappings.begin(),
        mappings.end(),
        astraea::memory::mapping_intent_less);

    if (const auto conflict = first_mapping_conflict(mappings);
        conflict.has_value()) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::mapping_overlap_conflict,
                std::monostate{},
                conflict->first,
                conflict->second));
    }

    if (const auto stack_overlap =
            first_stack_overlap(request.initial_stack.storage, mappings);
        stack_overlap.has_value()) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::initial_stack_overlaps_load_mapping,
                std::monostate{},
                *stack_overlap));
    }

    auto view = astraea::memory::InitializedImageView::create(
        request.image_bytes,
        mappings);
    if (!view.has_value()) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::initialized_image_failure,
                view.error()));
    }

    const ProgramHeader* dynamic_segment = nullptr;
    for (const auto& header : elf.program_headers) {
        if (header.type != kPtDynamic) {
            continue;
        }

        if (dynamic_segment != nullptr) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::duplicate_dynamic_segment,
                    std::monostate{},
                    header.index,
                    dynamic_segment->index));
        }

        dynamic_segment = &header;
    }

    std::optional<DynamicTable> dynamic_table;
    std::optional<DynamicStringMetadata> dynamic_strings;
    std::optional<DynamicSymbolTableDescriptor> dynamic_symbols;
    GeneralDynamicRelocationMetadata general_relocations{
        .rel = std::nullopt,
        .rela = std::nullopt,
    };
    std::optional<DynamicRelocationTableDescriptor> plt_relocations;

    if (dynamic_segment != nullptr) {
        auto table = parse_dynamic_table(
            request.image_bytes,
            *dynamic_segment);
        if (!table.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::dynamic_parse_failure,
                    table.error(),
                    dynamic_segment->index));
        }

        dynamic_table = std::move(table.value());

        auto strings = build_dynamic_string_metadata(*dynamic_table);
        if (!strings.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::dynamic_metadata_failure,
                    strings.error(),
                    dynamic_segment->index));
        }
        dynamic_strings = std::move(strings.value());

        auto symbols = build_dynamic_symbol_table_descriptor(
            *dynamic_table,
            view.value());
        if (!symbols.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::dynamic_symbol_failure,
                    symbols.error(),
                    dynamic_segment->index));
        }
        dynamic_symbols = symbols.value();

        auto general = build_general_dynamic_relocation_metadata(
            *dynamic_table);
        if (!general.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::dynamic_relocation_failure,
                    general.error(),
                    dynamic_segment->index));
        }
        general_relocations = general.value();

        auto plt = build_plt_dynamic_relocation_metadata(
            *dynamic_table);
        if (!plt.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::dynamic_relocation_failure,
                    plt.error(),
                    dynamic_segment->index));
        }
        plt_relocations = plt.value();

        const DynamicRelocationTableDescriptor* first_nonempty_relocation = nullptr;
        if (general_relocations.rel.has_value() &&
            general_relocations.rel->count > 0) {
            first_nonempty_relocation = &*general_relocations.rel;
        } else if (general_relocations.rela.has_value() &&
                   general_relocations.rela->count > 0) {
            first_nonempty_relocation = &*general_relocations.rela;
        } else if (plt_relocations.has_value() &&
                   plt_relocations->count > 0) {
            first_nonempty_relocation = &*plt_relocations;
        }

        if (first_nonempty_relocation != nullptr &&
            !dynamic_symbols.has_value()) {
            return GuestImageResult::failure(
                image_error(
                    GuestImageErrorCode::dynamic_relocation_failure,
                    DynamicRelocationError{
                        .code = DynamicRelocationErrorCode::missing_symbol_table,
                        .tag = std::nullopt,
                        .source_entry_index =
                            first_nonempty_relocation->address_source_entry_index,
                        .conflicting_entry_index = std::nullopt,
                        .table_kind = first_nonempty_relocation->kind,
                        .table_index = std::nullopt,
                        .guest_address =
                            first_nonempty_relocation->range.base(),
                        .symbol_index = std::nullopt,
                        .image_error = std::nullopt,
                    },
                    dynamic_segment->index));
        }
    }

    auto tls_descriptor = build_tls_template_descriptor(
        elf.program_headers,
        request.image_bytes);
    if (!tls_descriptor.has_value()) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::tls_failure,
                tls_descriptor.error(),
                tls_descriptor.error().source_program_header_index,
                tls_descriptor.error().conflicting_program_header_index));
    }

    auto stack = build_initial_stack(request.initial_stack);
    if (!stack.has_value()) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::initial_stack_failure,
                stack.error()));
    }

    return GuestImageResult::success(
        GuestImage{
            .image_bytes = std::move(request.image_bytes),
            .elf = std::move(elf),
            .mappings = std::move(mappings),
            .dynamic_table = std::move(dynamic_table),
            .dynamic_strings = std::move(dynamic_strings),
            .dynamic_symbols = std::move(dynamic_symbols),
            .general_relocations = std::move(general_relocations),
            .plt_relocations = std::move(plt_relocations),
            .tls = std::move(tls_descriptor.value()),
            .initial_stack = std::move(stack.value()),
        });
}

GuestImageResult build_guest_image(GuestImageRequest request) {
    try {
        return build_guest_image_impl(std::move(request));
    } catch (const std::bad_alloc&) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::host_allocation_failure));
    } catch (const std::length_error&) {
        return GuestImageResult::failure(
            image_error(
                GuestImageErrorCode::host_size_unrepresentable));
    }
}

}  // namespace astraea::loader
