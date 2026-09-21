#include <astraea/loader/dynamic_metadata.hpp>

#include <optional>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::int64_t kDtNeeded = 1;
constexpr std::int64_t kDtStrtab = 5;
constexpr std::int64_t kDtStrsz = 10;
constexpr std::int64_t kDtSoname = 14;

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

}  // namespace astraea::loader
