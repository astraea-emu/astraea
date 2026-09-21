#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic.hpp>
#include <astraea/loader/sysv_hash.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/initialized_image_view.hpp>

namespace astraea::loader {

enum class DynamicSymbolErrorCode {
    conflicting_dynamic_tag,
    missing_required_companion_tag,
    invalid_symbol_entry_size,
    invalid_symbol_table_size,
    symbol_count_unavailable,
    conflicting_symbol_count,
    symbol_table_size_overflow,
    symbol_table_range_overflow,
    sysv_hash_failure,
    symbol_index_out_of_bounds,
    symbol_entry_unreadable,
    invalid_undefined_symbol,
};

struct DynamicSymbolError {
    DynamicSymbolErrorCode code;
    std::optional<std::int64_t> tag;
    std::optional<std::size_t> source_entry_index;
    std::optional<std::size_t> conflicting_entry_index;
    std::optional<std::uint64_t> symbol_index;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<SysvHashError> sysv_hash_error;
    std::optional<astraea::memory::InitializedImageError> image_error;
};

struct DynamicSymbolTableDescriptor {
    astraea::memory::GuestRange range;
    astraea::memory::GuestSize entry_size;
    std::uint64_t symbol_count;
    bool count_from_symtabsz;
    bool count_from_sysv_hash;
    std::size_t symtab_source_entry_index;
    std::size_t syment_source_entry_index;
    std::optional<std::size_t> symtabsz_source_entry_index;
    std::optional<std::size_t> hash_source_entry_index;
};

struct DynamicSymbol {
    std::uint64_t index;
    std::uint32_t name_offset;
    std::uint8_t info;
    std::uint8_t other;
    std::uint16_t section_index_raw;
    std::uint64_t value;
    std::uint64_t size;

    [[nodiscard]] constexpr std::uint8_t binding() const noexcept {
        return static_cast<std::uint8_t>(info >> 4U);
    }

    [[nodiscard]] constexpr std::uint8_t type() const noexcept {
        return static_cast<std::uint8_t>(info & 0x0fU);
    }

    [[nodiscard]] constexpr std::uint8_t visibility() const noexcept {
        return static_cast<std::uint8_t>(other & 0x07U);
    }

    [[nodiscard]] constexpr bool is_extended_section_index() const noexcept {
        return section_index_raw == 0xffffU;
    }

    auto operator<=>(const DynamicSymbol&) const = default;
};

using DynamicSymbolDescriptorResult =
    astraea::core::Result<
        std::optional<DynamicSymbolTableDescriptor>,
        DynamicSymbolError>;

using DynamicSymbolResult =
    astraea::core::Result<DynamicSymbol, DynamicSymbolError>;

[[nodiscard]] DynamicSymbolDescriptorResult build_dynamic_symbol_table_descriptor(
    const DynamicTable& table,
    const astraea::memory::InitializedImageView& image_view);

[[nodiscard]] DynamicSymbolResult parse_dynamic_symbol(
    const DynamicSymbolTableDescriptor& descriptor,
    std::uint64_t index,
    const astraea::memory::InitializedImageView& image_view);

}  // namespace astraea::loader
