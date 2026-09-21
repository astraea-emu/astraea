#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::loader {

struct DynamicStringRef {
    std::uint64_t offset;
    std::size_t source_entry_index;

    auto operator<=>(const DynamicStringRef&) const = default;
};

struct DynamicStringTableDescriptor {
    astraea::memory::GuestRange range;

    auto operator<=>(const DynamicStringTableDescriptor&) const = default;
};

struct DynamicStringMetadata {
    std::optional<DynamicStringTableDescriptor> string_table;
    std::vector<DynamicStringRef> needed;
    std::optional<DynamicStringRef> soname;
};

enum class DynamicMetadataErrorCode {
    conflicting_dynamic_tag,
    missing_required_companion_tag,
    guest_table_range_overflow,
    missing_string_table,
};

struct DynamicMetadataError {
    DynamicMetadataErrorCode code;
    std::int64_t tag;
    std::optional<std::size_t> source_entry_index;
    std::optional<std::size_t> conflicting_entry_index;
};

using DynamicMetadataResult =
    astraea::core::Result<DynamicStringMetadata, DynamicMetadataError>;

[[nodiscard]] DynamicMetadataResult build_dynamic_string_metadata(
    const DynamicTable& table);

enum class SceDynamicTagKind {
    module_attributes,
    export_library_attributes,
    import_library_attributes,
    hash_table_size,
    symbol_table_size,
    original_filename,
    module_information,
    needed_module,
    export_library,
    import_library,
    unknown,
};

struct SceDynamicRecord {
    SceDynamicTagKind kind = SceDynamicTagKind::unknown;
    std::int64_t raw_tag = 0;
    std::uint64_t raw_value = 0;
    std::size_t source_entry_index = 0;

    auto operator<=>(const SceDynamicRecord&) const = default;
};

struct SceDynamicMetadata {
    std::vector<SceDynamicRecord> records;
};

enum class SceDynamicMetadataErrorCode {
    conflicting_singleton_tag,
    host_allocation_failure,
};

struct SceDynamicMetadataError {
    SceDynamicMetadataErrorCode code =
        SceDynamicMetadataErrorCode::host_allocation_failure;
    std::int64_t tag = 0;
    std::optional<std::size_t> source_entry_index;
    std::optional<std::size_t> conflicting_entry_index;

    auto operator<=>(const SceDynamicMetadataError&) const = default;
};

using SceDynamicMetadataResult =
    astraea::core::Result<
        SceDynamicMetadata,
        SceDynamicMetadataError>;

// Builds a data-only SCE view over already validated dynamic entries.
//
// Only current tag values documented by the #8 evidence map are classified.
// Raw tag/value/index data is retained for every entry, including generic,
// legacy, and otherwise unknown values. No module/library bit packing, NID
// meaning, dependency resolution, or HLE behavior is inferred here.
[[nodiscard]] SceDynamicMetadataResult
build_sce_dynamic_metadata(const DynamicTable& table);

}  // namespace astraea::loader
