#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic.hpp>
#include <astraea/loader/dynamic_symbols.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/initialized_image_view.hpp>

namespace astraea::loader {

enum class RelocationTableKind : std::uint8_t {
    rel,
    rela,
    plt_rel,
    plt_rela,
};

enum class DynamicRelocationErrorCode {
    conflicting_dynamic_tag,
    missing_required_companion_tag,
    invalid_relocation_entry_size,
    invalid_relocation_table_size,
    relocation_table_range_overflow,
    missing_symbol_table,
    relocation_index_out_of_bounds,
    relocation_entry_unreadable,
    relocation_symbol_index_out_of_bounds,
    invalid_plt_relocation_encoding,
};

struct DynamicRelocationError {
    DynamicRelocationErrorCode code;
    std::optional<std::int64_t> tag;
    std::optional<std::size_t> source_entry_index;
    std::optional<std::size_t> conflicting_entry_index;
    std::optional<RelocationTableKind> table_kind;
    std::optional<std::uint64_t> table_index;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<std::uint32_t> symbol_index;
    std::optional<astraea::memory::InitializedImageError> image_error;
};

struct DynamicRelocationTableDescriptor {
    RelocationTableKind kind;
    astraea::memory::GuestRange range;
    astraea::memory::GuestSize entry_size;
    std::uint64_t count;
    std::size_t address_source_entry_index;
    std::size_t size_source_entry_index;
    std::size_t encoding_source_entry_index;
};

struct GeneralDynamicRelocationMetadata {
    std::optional<DynamicRelocationTableDescriptor> rel;
    std::optional<DynamicRelocationTableDescriptor> rela;
};

struct DynamicRelocation {
    RelocationTableKind table_kind;
    std::uint64_t table_index;
    astraea::memory::GuestAddress target;
    std::uint64_t raw_info;
    std::uint32_t symbol_index;
    std::uint32_t relocation_type;
    std::optional<std::int64_t> addend;

    auto operator<=>(const DynamicRelocation&) const = default;
};

using GeneralDynamicRelocationResult =
    astraea::core::Result<
        GeneralDynamicRelocationMetadata,
        DynamicRelocationError>;

using DynamicRelocationResult =
    astraea::core::Result<DynamicRelocation, DynamicRelocationError>;

using PltDynamicRelocationResult =
    astraea::core::Result<
        std::optional<DynamicRelocationTableDescriptor>,
        DynamicRelocationError>;

[[nodiscard]] GeneralDynamicRelocationResult
build_general_dynamic_relocation_metadata(const DynamicTable& table);

[[nodiscard]] PltDynamicRelocationResult
build_plt_dynamic_relocation_metadata(const DynamicTable& table);

[[nodiscard]] DynamicRelocationResult parse_dynamic_relocation(
    const DynamicRelocationTableDescriptor& descriptor,
    std::uint64_t index,
    const DynamicSymbolTableDescriptor& symbols,
    const astraea::memory::InitializedImageView& image_view);

}  // namespace astraea::loader
