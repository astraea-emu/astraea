#include <astraea/loader/dynamic_relocations.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::int64_t kDtPltrelsz = 2;
constexpr std::int64_t kDtRela = 7;
constexpr std::int64_t kDtRelasz = 8;
constexpr std::int64_t kDtRelaent = 9;
constexpr std::int64_t kDtRel = 17;
constexpr std::int64_t kDtRelsz = 18;
constexpr std::int64_t kDtRelent = 19;
constexpr std::int64_t kDtPltrel = 20;
constexpr std::int64_t kDtJmprel = 23;

constexpr std::uint64_t kElf64RelSize = 16;
constexpr std::uint64_t kElf64RelaSize = 24;
constexpr std::size_t kMaxRelocationRecordSize = 24;

struct SingletonValue {
    std::uint64_t value;
    std::size_t source_entry_index;
};

struct GroupValues {
    std::optional<SingletonValue> address;
    std::optional<SingletonValue> size;
    std::optional<SingletonValue> entry_size;
};

[[nodiscard]] DynamicRelocationError relocation_error(
    DynamicRelocationErrorCode code,
    std::optional<std::int64_t> tag = std::nullopt,
    std::optional<std::size_t> source_entry_index = std::nullopt,
    std::optional<std::size_t> conflicting_entry_index = std::nullopt,
    std::optional<RelocationTableKind> table_kind = std::nullopt,
    std::optional<std::uint64_t> table_index = std::nullopt,
    std::optional<astraea::memory::GuestAddress> guest_address = std::nullopt,
    std::optional<std::uint32_t> symbol_index = std::nullopt,
    std::optional<astraea::memory::InitializedImageError> image_error = std::nullopt) {
    return DynamicRelocationError{
        .code = code,
        .tag = tag,
        .source_entry_index = source_entry_index,
        .conflicting_entry_index = conflicting_entry_index,
        .table_kind = table_kind,
        .table_index = table_index,
        .guest_address = guest_address,
        .symbol_index = symbol_index,
        .image_error = std::move(image_error),
    };
}

[[nodiscard]] astraea::core::Result<SingletonValue, DynamicRelocationError>
merge_singleton(
    std::optional<SingletonValue>& slot,
    const DynamicEntry& entry) {
    if (!slot.has_value()) {
        slot = SingletonValue{
            .value = entry.value,
            .source_entry_index = entry.index,
        };
        return astraea::core::Result<
            SingletonValue,
            DynamicRelocationError>::success(*slot);
    }

    if (slot->value != entry.value) {
        return astraea::core::Result<
            SingletonValue,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::conflicting_dynamic_tag,
                entry.tag,
                entry.index,
                slot->source_entry_index));
    }

    return astraea::core::Result<
        SingletonValue,
        DynamicRelocationError>::success(*slot);
}

[[nodiscard]] bool group_active(const GroupValues& group) noexcept {
    return group.address.has_value() ||
           group.size.has_value() ||
           group.entry_size.has_value();
}

[[nodiscard]] astraea::core::Result<
    std::optional<DynamicRelocationTableDescriptor>,
    DynamicRelocationError>
build_group(
    const GroupValues& group,
    RelocationTableKind kind,
    std::int64_t address_tag,
    std::int64_t size_tag,
    std::int64_t entry_tag,
    std::uint64_t required_entry_size) {
    if (!group_active(group)) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::success(std::nullopt);
    }

    if (!group.address.has_value()) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::missing_required_companion_tag,
                address_tag,
                std::nullopt,
                std::nullopt,
                kind));
    }

    if (!group.size.has_value()) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::missing_required_companion_tag,
                size_tag,
                group.address->source_entry_index,
                std::nullopt,
                kind));
    }

    if (!group.entry_size.has_value()) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::missing_required_companion_tag,
                entry_tag,
                group.address->source_entry_index,
                std::nullopt,
                kind));
    }

    if (group.entry_size->value != required_entry_size) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::invalid_relocation_entry_size,
                entry_tag,
                group.entry_size->source_entry_index,
                std::nullopt,
                kind));
    }

    if ((group.size->value % required_entry_size) != 0) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::invalid_relocation_table_size,
                size_tag,
                group.size->source_entry_index,
                std::nullopt,
                kind));
    }

    auto range = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{group.address->value},
        astraea::memory::GuestSize{group.size->value});
    if (!range.has_value()) {
        return astraea::core::Result<
            std::optional<DynamicRelocationTableDescriptor>,
            DynamicRelocationError>::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_table_range_overflow,
                address_tag,
                group.address->source_entry_index,
                std::nullopt,
                kind,
                std::nullopt,
                astraea::memory::GuestAddress{group.address->value}));
    }

    return astraea::core::Result<
        std::optional<DynamicRelocationTableDescriptor>,
        DynamicRelocationError>::success(
        DynamicRelocationTableDescriptor{
            .kind = kind,
            .range = range.value(),
            .entry_size = astraea::memory::GuestSize{required_entry_size},
            .count = group.size->value / required_entry_size,
            .address_source_entry_index = group.address->source_entry_index,
            .size_source_entry_index = group.size->source_entry_index,
            .encoding_source_entry_index = group.entry_size->source_entry_index,
        });
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
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] std::optional<std::uint64_t> expected_entry_size(
    RelocationTableKind kind) noexcept {
    switch (kind) {
    case RelocationTableKind::rel:
    case RelocationTableKind::plt_rel:
        return kElf64RelSize;
    case RelocationTableKind::rela:
    case RelocationTableKind::plt_rela:
        return kElf64RelaSize;
    }
    return std::nullopt;
}

}  // namespace

GeneralDynamicRelocationResult build_general_dynamic_relocation_metadata(
    const DynamicTable& table) {
    GroupValues rel;
    GroupValues rela;

    for (const auto& entry : table.entries) {
        std::optional<SingletonValue>* slot = nullptr;

        switch (entry.tag) {
        case kDtRela:
            slot = &rela.address;
            break;
        case kDtRelasz:
            slot = &rela.size;
            break;
        case kDtRelaent:
            slot = &rela.entry_size;
            break;
        case kDtRel:
            slot = &rel.address;
            break;
        case kDtRelsz:
            slot = &rel.size;
            break;
        case kDtRelent:
            slot = &rel.entry_size;
            break;
        default:
            break;
        }

        if (slot != nullptr) {
            auto merged = merge_singleton(*slot, entry);
            if (!merged.has_value()) {
                return GeneralDynamicRelocationResult::failure(merged.error());
            }
        }
    }

    auto rel_descriptor = build_group(
        rel,
        RelocationTableKind::rel,
        kDtRel,
        kDtRelsz,
        kDtRelent,
        kElf64RelSize);
    if (!rel_descriptor.has_value()) {
        return GeneralDynamicRelocationResult::failure(rel_descriptor.error());
    }

    auto rela_descriptor = build_group(
        rela,
        RelocationTableKind::rela,
        kDtRela,
        kDtRelasz,
        kDtRelaent,
        kElf64RelaSize);
    if (!rela_descriptor.has_value()) {
        return GeneralDynamicRelocationResult::failure(rela_descriptor.error());
    }

    return GeneralDynamicRelocationResult::success(
        GeneralDynamicRelocationMetadata{
            .rel = rel_descriptor.value(),
            .rela = rela_descriptor.value(),
        });
}

PltDynamicRelocationResult build_plt_dynamic_relocation_metadata(
    const DynamicTable& table) {
    std::optional<SingletonValue> address;
    std::optional<SingletonValue> size;
    std::optional<SingletonValue> encoding;

    for (const auto& entry : table.entries) {
        std::optional<SingletonValue>* slot = nullptr;

        switch (entry.tag) {
        case kDtJmprel:
            slot = &address;
            break;
        case kDtPltrelsz:
            slot = &size;
            break;
        case kDtPltrel:
            slot = &encoding;
            break;
        default:
            break;
        }

        if (slot != nullptr) {
            auto merged = merge_singleton(*slot, entry);
            if (!merged.has_value()) {
                return PltDynamicRelocationResult::failure(merged.error());
            }
        }
    }

    const bool active =
        address.has_value() || size.has_value() || encoding.has_value();
    if (!active) {
        return PltDynamicRelocationResult::success(std::nullopt);
    }

    if (!address.has_value()) {
        return PltDynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::missing_required_companion_tag,
                kDtJmprel));
    }

    if (!size.has_value()) {
        return PltDynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::missing_required_companion_tag,
                kDtPltrelsz,
                address->source_entry_index));
    }

    if (!encoding.has_value()) {
        return PltDynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::missing_required_companion_tag,
                kDtPltrel,
                address->source_entry_index));
    }

    RelocationTableKind kind;
    std::uint64_t entry_size = 0;
    if (encoding->value == static_cast<std::uint64_t>(kDtRel)) {
        kind = RelocationTableKind::plt_rel;
        entry_size = kElf64RelSize;
    } else if (encoding->value == static_cast<std::uint64_t>(kDtRela)) {
        kind = RelocationTableKind::plt_rela;
        entry_size = kElf64RelaSize;
    } else {
        return PltDynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::invalid_plt_relocation_encoding,
                kDtPltrel,
                encoding->source_entry_index));
    }

    if ((size->value % entry_size) != 0) {
        return PltDynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::invalid_relocation_table_size,
                kDtPltrelsz,
                size->source_entry_index,
                std::nullopt,
                kind));
    }

    auto range = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{address->value},
        astraea::memory::GuestSize{size->value});
    if (!range.has_value()) {
        return PltDynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_table_range_overflow,
                kDtJmprel,
                address->source_entry_index,
                std::nullopt,
                kind,
                std::nullopt,
                astraea::memory::GuestAddress{address->value}));
    }

    return PltDynamicRelocationResult::success(
        DynamicRelocationTableDescriptor{
            .kind = kind,
            .range = range.value(),
            .entry_size = astraea::memory::GuestSize{entry_size},
            .count = size->value / entry_size,
            .address_source_entry_index = address->source_entry_index,
            .size_source_entry_index = size->source_entry_index,
            .encoding_source_entry_index = encoding->source_entry_index,
        });
}

DynamicRelocationResult parse_dynamic_relocation(
    const DynamicRelocationTableDescriptor& descriptor,
    std::uint64_t index,
    const DynamicSymbolTableDescriptor& symbols,
    const astraea::memory::InitializedImageView& image_view) {
    const auto required_size = expected_entry_size(descriptor.kind);
    if (!required_size.has_value() ||
        descriptor.entry_size.value() != required_size.value()) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::invalid_relocation_entry_size,
                std::nullopt,
                descriptor.encoding_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index));
    }

    if (index >= descriptor.count) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_index_out_of_bounds,
                std::nullopt,
                descriptor.address_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index));
    }

    if (index >
        std::numeric_limits<std::uint64_t>::max() / required_size.value()) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_table_range_overflow,
                std::nullopt,
                descriptor.address_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index,
                descriptor.range.base()));
    }

    const auto byte_offset = index * required_size.value();
    auto address = astraea::memory::GuestAddress::checked_add(
        descriptor.range.base(),
        astraea::memory::GuestSize{byte_offset});
    if (!address.has_value()) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_table_range_overflow,
                std::nullopt,
                descriptor.address_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index,
                descriptor.range.base()));
    }

    auto record_range = astraea::memory::GuestRange::create(
        address.value(),
        astraea::memory::GuestSize{required_size.value()});
    if (!record_range.has_value()) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_table_range_overflow,
                std::nullopt,
                descriptor.address_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index,
                address.value()));
    }

    std::array<std::byte, kMaxRelocationRecordSize> bytes{};
    auto output = std::span<std::byte>{bytes}.first(
        static_cast<std::size_t>(required_size.value()));
    auto copied = image_view.copy_bytes(record_range.value(), output);
    if (!copied.has_value()) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_entry_unreadable,
                std::nullopt,
                descriptor.address_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index,
                copied.error().guest_address.value_or(address.value()),
                std::nullopt,
                copied.error()));
    }

    const auto target_raw = read_little_endian<std::uint64_t>(bytes, 0);
    const auto raw_info = read_little_endian<std::uint64_t>(bytes, 8);
    const auto symbol_index = static_cast<std::uint32_t>(raw_info >> 32U);
    const auto relocation_type =
        static_cast<std::uint32_t>(raw_info & 0xffffffffULL);

    if (static_cast<std::uint64_t>(symbol_index) >= symbols.symbol_count) {
        return DynamicRelocationResult::failure(
            relocation_error(
                DynamicRelocationErrorCode::relocation_symbol_index_out_of_bounds,
                std::nullopt,
                descriptor.address_source_entry_index,
                std::nullopt,
                descriptor.kind,
                index,
                astraea::memory::GuestAddress{target_raw},
                symbol_index));
    }

    std::optional<std::int64_t> addend;
    if (descriptor.kind == RelocationTableKind::rela ||
        descriptor.kind == RelocationTableKind::plt_rela) {
        const auto raw_addend = read_little_endian<std::uint64_t>(bytes, 16);
        addend = std::bit_cast<std::int64_t>(raw_addend);
    }

    return DynamicRelocationResult::success(
        DynamicRelocation{
            .table_kind = descriptor.kind,
            .table_index = index,
            .target = astraea::memory::GuestAddress{target_raw},
            .raw_info = raw_info,
            .symbol_index = symbol_index,
            .relocation_type = relocation_type,
            .addend = addend,
        });
}

}  // namespace astraea::loader
