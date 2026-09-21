#include <astraea/loader/dynamic_symbols.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::int64_t kDtHash = 4;
constexpr std::int64_t kDtSymtab = 6;
constexpr std::int64_t kDtSyment = 11;
constexpr std::int64_t kDtSymtabsz = 39;
constexpr std::uint64_t kElf64SymbolSize = 24;

struct SingletonValue {
    std::uint64_t value;
    std::size_t source_entry_index;
};

[[nodiscard]] DynamicSymbolError symbol_error(
    DynamicSymbolErrorCode code,
    std::optional<std::int64_t> tag = std::nullopt,
    std::optional<std::size_t> source_entry_index = std::nullopt,
    std::optional<std::size_t> conflicting_entry_index = std::nullopt,
    std::optional<std::uint64_t> symbol_index = std::nullopt,
    std::optional<astraea::memory::GuestAddress> guest_address = std::nullopt,
    std::optional<SysvHashError> sysv_hash_error = std::nullopt,
    std::optional<astraea::memory::InitializedImageError> image_error = std::nullopt) {
    return DynamicSymbolError{
        .code = code,
        .tag = tag,
        .source_entry_index = source_entry_index,
        .conflicting_entry_index = conflicting_entry_index,
        .symbol_index = symbol_index,
        .guest_address = guest_address,
        .sysv_hash_error = std::move(sysv_hash_error),
        .image_error = std::move(image_error),
    };
}

[[nodiscard]] astraea::core::Result<SingletonValue, DynamicSymbolError> merge_singleton(
    std::optional<SingletonValue>& slot,
    const DynamicEntry& entry) {
    if (!slot.has_value()) {
        slot = SingletonValue{
            .value = entry.value,
            .source_entry_index = entry.index,
        };
        return astraea::core::Result<SingletonValue, DynamicSymbolError>::success(*slot);
    }

    if (slot->value != entry.value) {
        return astraea::core::Result<SingletonValue, DynamicSymbolError>::failure(
            symbol_error(
                DynamicSymbolErrorCode::conflicting_dynamic_tag,
                entry.tag,
                entry.index,
                slot->source_entry_index));
    }

    return astraea::core::Result<SingletonValue, DynamicSymbolError>::success(*slot);
}

template <typename T>
[[nodiscard]] T read_little_endian(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte =
            static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] DynamicSymbolResult read_symbol(
    const DynamicSymbolTableDescriptor& descriptor,
    std::uint64_t index,
    const astraea::memory::InitializedImageView& image_view,
    bool enforce_bounds) {
    if (enforce_bounds && index >= descriptor.symbol_count) {
        return DynamicSymbolResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_index_out_of_bounds,
                std::nullopt,
                descriptor.symtab_source_entry_index,
                std::nullopt,
                index));
    }

    if (index > std::numeric_limits<std::uint64_t>::max() / kElf64SymbolSize) {
        return DynamicSymbolResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_table_size_overflow,
                std::nullopt,
                descriptor.symtab_source_entry_index,
                std::nullopt,
                index));
    }

    const auto byte_offset = index * kElf64SymbolSize;
    auto address = astraea::memory::GuestAddress::checked_add(
        descriptor.range.base(),
        astraea::memory::GuestSize{byte_offset});
    if (!address.has_value()) {
        return DynamicSymbolResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_table_range_overflow,
                std::nullopt,
                descriptor.symtab_source_entry_index,
                std::nullopt,
                index,
                descriptor.range.base()));
    }

    auto symbol_range = astraea::memory::GuestRange::create(
        address.value(),
        astraea::memory::GuestSize{kElf64SymbolSize});
    if (!symbol_range.has_value()) {
        return DynamicSymbolResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_table_range_overflow,
                std::nullopt,
                descriptor.symtab_source_entry_index,
                std::nullopt,
                index,
                address.value()));
    }

    std::array<std::byte, kElf64SymbolSize> bytes{};
    auto copied = image_view.copy_bytes(symbol_range.value(), bytes);
    if (!copied.has_value()) {
        return DynamicSymbolResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_entry_unreadable,
                std::nullopt,
                descriptor.symtab_source_entry_index,
                std::nullopt,
                index,
                copied.error().guest_address.value_or(address.value()),
                std::nullopt,
                copied.error()));
    }

    return DynamicSymbolResult::success(
        DynamicSymbol{
            .index = index,
            .name_offset = read_little_endian<std::uint32_t>(bytes, 0),
            .info = std::to_integer<std::uint8_t>(bytes[4]),
            .other = std::to_integer<std::uint8_t>(bytes[5]),
            .section_index_raw = read_little_endian<std::uint16_t>(bytes, 6),
            .value = read_little_endian<std::uint64_t>(bytes, 8),
            .size = read_little_endian<std::uint64_t>(bytes, 16),
        });
}

[[nodiscard]] bool is_reserved_undefined_symbol(const DynamicSymbol& symbol) noexcept {
    return symbol.index == 0 &&
           symbol.name_offset == 0 &&
           symbol.info == 0 &&
           symbol.other == 0 &&
           symbol.section_index_raw == 0 &&
           symbol.value == 0 &&
           symbol.size == 0;
}

}  // namespace

DynamicSymbolDescriptorResult build_dynamic_symbol_table_descriptor(
    const DynamicTable& table,
    const astraea::memory::InitializedImageView& image_view) {
    std::optional<SingletonValue> symtab;
    std::optional<SingletonValue> syment;
    std::optional<SingletonValue> symtabsz;
    bool has_hash = false;

    for (const auto& entry : table.entries) {
        switch (entry.tag) {
        case kDtSymtab: {
            auto merged = merge_singleton(symtab, entry);
            if (!merged.has_value()) {
                return DynamicSymbolDescriptorResult::failure(merged.error());
            }
            break;
        }
        case kDtSyment: {
            auto merged = merge_singleton(syment, entry);
            if (!merged.has_value()) {
                return DynamicSymbolDescriptorResult::failure(merged.error());
            }
            break;
        }
        case kDtSymtabsz: {
            auto merged = merge_singleton(symtabsz, entry);
            if (!merged.has_value()) {
                return DynamicSymbolDescriptorResult::failure(merged.error());
            }
            break;
        }
        case kDtHash:
            has_hash = true;
            break;
        default:
            break;
        }
    }

    const bool any_symbol_metadata =
        symtab.has_value() || syment.has_value() || symtabsz.has_value() || has_hash;
    if (!any_symbol_metadata) {
        return DynamicSymbolDescriptorResult::success(std::nullopt);
    }

    if (!symtab.has_value()) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::missing_required_companion_tag,
                kDtSymtab));
    }

    if (!syment.has_value()) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::missing_required_companion_tag,
                kDtSyment,
                symtab->source_entry_index));
    }

    if (syment->value != kElf64SymbolSize) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::invalid_symbol_entry_size,
                kDtSyment,
                syment->source_entry_index));
    }

    std::optional<std::uint64_t> symtabsz_count;
    if (symtabsz.has_value()) {
        if (symtabsz->value == 0 || (symtabsz->value % kElf64SymbolSize) != 0) {
            return DynamicSymbolDescriptorResult::failure(
                symbol_error(
                    DynamicSymbolErrorCode::invalid_symbol_table_size,
                    kDtSymtabsz,
                    symtabsz->source_entry_index));
        }

        symtabsz_count = symtabsz->value / kElf64SymbolSize;
        if (*symtabsz_count == 0) {
            return DynamicSymbolDescriptorResult::failure(
                symbol_error(
                    DynamicSymbolErrorCode::invalid_symbol_table_size,
                    kDtSymtabsz,
                    symtabsz->source_entry_index));
        }
    }

    auto sysv = build_sysv_hash_count_evidence(table, image_view);
    if (!sysv.has_value()) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::sysv_hash_failure,
                kDtHash,
                sysv.error().source_entry_index,
                sysv.error().conflicting_entry_index,
                std::nullopt,
                sysv.error().guest_address,
                sysv.error()));
    }

    std::optional<std::uint64_t> hash_count;
    std::optional<std::size_t> hash_source_index;
    if (sysv->has_value()) {
        hash_count = static_cast<std::uint64_t>(sysv->value().symbol_count);
        hash_source_index = sysv->value().source_entry_index;
    }

    if (!symtabsz_count.has_value() && !hash_count.has_value()) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_count_unavailable,
                std::nullopt,
                symtab->source_entry_index));
    }

    if (symtabsz_count.has_value() &&
        hash_count.has_value() &&
        *symtabsz_count != *hash_count) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::conflicting_symbol_count,
                std::nullopt,
                symtabsz->source_entry_index,
                hash_source_index));
    }

    const auto count =
        symtabsz_count.has_value() ? *symtabsz_count : *hash_count;
    if (count == 0) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::invalid_symbol_table_size,
                std::nullopt,
                symtab->source_entry_index));
    }

    if (count > std::numeric_limits<std::uint64_t>::max() / kElf64SymbolSize) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_table_size_overflow,
                std::nullopt,
                symtab->source_entry_index));
    }

    const auto byte_size = count * kElf64SymbolSize;
    auto table_range = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{symtab->value},
        astraea::memory::GuestSize{byte_size});
    if (!table_range.has_value()) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::symbol_table_range_overflow,
                kDtSymtab,
                symtab->source_entry_index,
                std::nullopt,
                std::nullopt,
                astraea::memory::GuestAddress{symtab->value}));
    }

    DynamicSymbolTableDescriptor descriptor{
        .range = table_range.value(),
        .entry_size = astraea::memory::GuestSize{kElf64SymbolSize},
        .symbol_count = count,
        .count_from_symtabsz = symtabsz_count.has_value(),
        .count_from_sysv_hash = hash_count.has_value(),
        .symtab_source_entry_index = symtab->source_entry_index,
        .syment_source_entry_index = syment->source_entry_index,
        .symtabsz_source_entry_index =
            symtabsz.has_value()
                ? std::optional<std::size_t>{symtabsz->source_entry_index}
                : std::nullopt,
        .hash_source_entry_index = hash_source_index,
    };

    auto zero = read_symbol(descriptor, 0, image_view, false);
    if (!zero.has_value()) {
        return DynamicSymbolDescriptorResult::failure(zero.error());
    }

    if (!is_reserved_undefined_symbol(zero.value())) {
        return DynamicSymbolDescriptorResult::failure(
            symbol_error(
                DynamicSymbolErrorCode::invalid_undefined_symbol,
                std::nullopt,
                symtab->source_entry_index,
                std::nullopt,
                0,
                descriptor.range.base()));
    }

    return DynamicSymbolDescriptorResult::success(descriptor);
}

DynamicSymbolResult parse_dynamic_symbol(
    const DynamicSymbolTableDescriptor& descriptor,
    std::uint64_t index,
    const astraea::memory::InitializedImageView& image_view) {
    return read_symbol(descriptor, index, image_view, true);
}

}  // namespace astraea::loader
