#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/elf64.hpp>

namespace astraea::loader {

enum class DynamicErrorCode {
    not_dynamic_segment,
    integer_overflow,
    segment_out_of_bounds,
    invalid_segment_size,
    missing_terminator,
};

struct DynamicError {
    DynamicErrorCode code;
    std::optional<std::size_t> entry_index;
    std::uint64_t file_offset;
};

struct DynamicEntry {
    std::int64_t tag;
    std::uint64_t value;
    std::size_t index;

    auto operator<=>(const DynamicEntry&) const = default;
};

struct DynamicTable {
    std::vector<DynamicEntry> entries;
};

using DynamicParseResult = astraea::core::Result<DynamicTable, DynamicError>;

[[nodiscard]] DynamicParseResult parse_dynamic_table(
    std::span<const std::byte> bytes,
    const ProgramHeader& dynamic_segment);

}  // namespace astraea::loader
