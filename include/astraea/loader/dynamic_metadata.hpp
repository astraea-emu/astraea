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

}  // namespace astraea::loader
