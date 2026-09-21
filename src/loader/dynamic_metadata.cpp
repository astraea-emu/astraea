#include <astraea/loader/dynamic_metadata.hpp>

#include <array>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::int64_t kDtNeeded = 1;
constexpr std::int64_t kDtStrtab = 5;
constexpr std::int64_t kDtStrsz = 10;
constexpr std::int64_t kDtSoname = 14;

constexpr std::int64_t kDtSceModuleAttributes = 0x61000011;
constexpr std::int64_t kDtSceExportLibraryAttributes = 0x61000017;
constexpr std::int64_t kDtSceImportLibraryAttributes = 0x61000019;
constexpr std::int64_t kDtSceHashTableSize = 0x6100003d;
constexpr std::int64_t kDtSceSymbolTableSize = 0x6100003f;
constexpr std::int64_t kDtSceOriginalFilename = 0x61000041;
constexpr std::int64_t kDtSceModuleInformation = 0x61000043;
constexpr std::int64_t kDtSceNeededModule = 0x61000045;
constexpr std::int64_t kDtSceExportLibrary = 0x61000047;
constexpr std::int64_t kDtSceImportLibrary = 0x61000049;

struct SingletonValue {
    std::uint64_t value;
    std::size_t source_entry_index;
};

[[nodiscard]] DynamicMetadataError metadata_error(
    DynamicMetadataErrorCode code,
    std::int64_t tag,
    std::optional<std::size_t> source_entry_index = std::nullopt,
    std::optional<std::size_t> conflicting_entry_index = std::nullopt) {
    return DynamicMetadataError{
        .code = code,
        .tag = tag,
        .source_entry_index = source_entry_index,
        .conflicting_entry_index = conflicting_entry_index,
    };
}

[[nodiscard]] astraea::core::Result<SingletonValue, DynamicMetadataError> merge_singleton(
    std::optional<SingletonValue>& slot,
    const DynamicEntry& entry) {
    if (!slot.has_value()) {
        slot = SingletonValue{
            .value = entry.value,
            .source_entry_index = entry.index,
        };
        return astraea::core::Result<SingletonValue, DynamicMetadataError>::success(*slot);
    }

    if (slot->value != entry.value) {
        return astraea::core::Result<SingletonValue, DynamicMetadataError>::failure(
            metadata_error(
                DynamicMetadataErrorCode::conflicting_dynamic_tag,
                entry.tag,
                entry.index,
                slot->source_entry_index));
    }

    return astraea::core::Result<SingletonValue, DynamicMetadataError>::success(*slot);
}

[[nodiscard]] SceDynamicTagKind classify_sce_dynamic_tag(
    std::int64_t tag) noexcept {
    switch (tag) {
    case kDtSceModuleAttributes:
        return SceDynamicTagKind::module_attributes;
    case kDtSceExportLibraryAttributes:
        return SceDynamicTagKind::export_library_attributes;
    case kDtSceImportLibraryAttributes:
        return SceDynamicTagKind::import_library_attributes;
    case kDtSceHashTableSize:
        return SceDynamicTagKind::hash_table_size;
    case kDtSceSymbolTableSize:
        return SceDynamicTagKind::symbol_table_size;
    case kDtSceOriginalFilename:
        return SceDynamicTagKind::original_filename;
    case kDtSceModuleInformation:
        return SceDynamicTagKind::module_information;
    case kDtSceNeededModule:
        return SceDynamicTagKind::needed_module;
    case kDtSceExportLibrary:
        return SceDynamicTagKind::export_library;
    case kDtSceImportLibrary:
        return SceDynamicTagKind::import_library;
    default:
        return SceDynamicTagKind::unknown;
    }
}

[[nodiscard]] std::optional<std::size_t>
sce_singleton_slot(SceDynamicTagKind kind) noexcept {
    switch (kind) {
    case SceDynamicTagKind::module_attributes:
        return 0;
    case SceDynamicTagKind::hash_table_size:
        return 1;
    case SceDynamicTagKind::symbol_table_size:
        return 2;
    case SceDynamicTagKind::original_filename:
        return 3;
    case SceDynamicTagKind::module_information:
        return 4;

    case SceDynamicTagKind::export_library_attributes:
    case SceDynamicTagKind::import_library_attributes:
    case SceDynamicTagKind::needed_module:
    case SceDynamicTagKind::export_library:
    case SceDynamicTagKind::import_library:
    case SceDynamicTagKind::unknown:
        return std::nullopt;
    }

    return std::nullopt;
}

[[nodiscard]] SceDynamicMetadataError sce_metadata_error(
    SceDynamicMetadataErrorCode code,
    std::int64_t tag = 0,
    std::optional<std::size_t> source_entry_index = std::nullopt,
    std::optional<std::size_t> conflicting_entry_index = std::nullopt) noexcept {
    return SceDynamicMetadataError{
        .code = code,
        .tag = tag,
        .source_entry_index = source_entry_index,
        .conflicting_entry_index = conflicting_entry_index,
    };
}

}  // namespace

DynamicMetadataResult build_dynamic_string_metadata(const DynamicTable& table) {
    std::optional<SingletonValue> strtab;
    std::optional<SingletonValue> strsz;
    std::optional<SingletonValue> soname;
    std::vector<DynamicStringRef> needed;

    for (const auto& entry : table.entries) {
        switch (entry.tag) {
        case kDtNeeded:
            needed.push_back(DynamicStringRef{
                .offset = entry.value,
                .source_entry_index = entry.index,
            });
            break;

        case kDtStrtab: {
            auto merged = merge_singleton(strtab, entry);
            if (!merged.has_value()) {
                return DynamicMetadataResult::failure(merged.error());
            }
            break;
        }

        case kDtStrsz: {
            auto merged = merge_singleton(strsz, entry);
            if (!merged.has_value()) {
                return DynamicMetadataResult::failure(merged.error());
            }
            break;
        }

        case kDtSoname: {
            auto merged = merge_singleton(soname, entry);
            if (!merged.has_value()) {
                return DynamicMetadataResult::failure(merged.error());
            }
            break;
        }

        default:
            break;
        }
    }

    const bool has_strtab = strtab.has_value();
    const bool has_strsz = strsz.has_value();

    if (has_strtab != has_strsz) {
        const auto missing_tag = has_strtab ? kDtStrsz : kDtStrtab;
        const auto source_index =
            has_strtab ? strtab->source_entry_index : strsz->source_entry_index;
        return DynamicMetadataResult::failure(
            metadata_error(
                DynamicMetadataErrorCode::missing_required_companion_tag,
                missing_tag,
                source_index));
    }

    const bool has_string_references = !needed.empty() || soname.has_value();
    if (has_string_references && !has_strtab) {
        const auto source_index =
            !needed.empty() ? needed.front().source_entry_index : soname->source_entry_index;
        const auto source_tag = !needed.empty() ? kDtNeeded : kDtSoname;
        return DynamicMetadataResult::failure(
            metadata_error(
                DynamicMetadataErrorCode::missing_string_table,
                source_tag,
                source_index));
    }

    DynamicStringMetadata metadata{
        .string_table = std::nullopt,
        .needed = std::move(needed),
        .soname = std::nullopt,
    };

    if (soname.has_value()) {
        metadata.soname = DynamicStringRef{
            .offset = soname->value,
            .source_entry_index = soname->source_entry_index,
        };
    }

    if (has_strtab) {
        auto range = astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{strtab->value},
            astraea::memory::GuestSize{strsz->value});
        if (!range.has_value()) {
            return DynamicMetadataResult::failure(
                metadata_error(
                    DynamicMetadataErrorCode::guest_table_range_overflow,
                    kDtStrtab,
                    strtab->source_entry_index,
                    strsz->source_entry_index));
        }

        metadata.string_table = DynamicStringTableDescriptor{
            .range = range.value(),
        };
    }

    return DynamicMetadataResult::success(std::move(metadata));
}

SceDynamicMetadataResult
build_sce_dynamic_metadata(const DynamicTable& table) {
    try {
        SceDynamicMetadata metadata;
        metadata.records.reserve(table.entries.size());

        std::array<std::optional<SingletonValue>, 5>
            singleton_values{};

        for (const auto& entry : table.entries) {
            const auto kind =
                classify_sce_dynamic_tag(entry.tag);

            if (const auto slot =
                    sce_singleton_slot(kind);
                slot.has_value()) {
                auto& existing =
                    singleton_values[slot.value()];

                if (!existing.has_value()) {
                    existing = SingletonValue{
                        .value = entry.value,
                        .source_entry_index =
                            entry.index,
                    };
                } else if (
                    existing->value != entry.value) {
                    return SceDynamicMetadataResult::
                        failure(
                            sce_metadata_error(
                                SceDynamicMetadataErrorCode::
                                    conflicting_singleton_tag,
                                entry.tag,
                                entry.index,
                                existing->
                                    source_entry_index));
                }
            }

            metadata.records.push_back(
                SceDynamicRecord{
                    .kind = kind,
                    .raw_tag = entry.tag,
                    .raw_value = entry.value,
                    .source_entry_index =
                        entry.index,
                });
        }

        return SceDynamicMetadataResult::success(
            std::move(metadata));
    } catch (const std::bad_alloc&) {
        return SceDynamicMetadataResult::failure(
            sce_metadata_error(
                SceDynamicMetadataErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return SceDynamicMetadataResult::failure(
            sce_metadata_error(
                SceDynamicMetadataErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::loader
