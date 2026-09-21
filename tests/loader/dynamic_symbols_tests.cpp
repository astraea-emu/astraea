#include <astraea/loader/dynamic_symbols.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::DynamicEntry;
using astraea::loader::DynamicSymbolErrorCode;
using astraea::loader::DynamicTable;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::InitializedImageView;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

constexpr std::int64_t kDtHash = 4;
constexpr std::int64_t kDtSymtab = 6;
constexpr std::int64_t kDtSyment = 11;
constexpr std::int64_t kDtSymtabsz = 39;
constexpr std::uint64_t kSymSize = 24;

DynamicEntry entry(std::int64_t tag, std::uint64_t value, std::size_t index) {
    return DynamicEntry{.tag = tag, .value = value, .index = index};
}

DynamicTable table(std::initializer_list<DynamicEntry> entries) {
    return DynamicTable{.entries = std::vector<DynamicEntry>{entries}};
}

GuestRange range(std::uint64_t base, std::uint64_t size) {
    auto result = GuestRange::create(GuestAddress{base}, GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

GuestPermissions read_only() {
    auto result = GuestPermissions::checked_from_bits(0x4U);
    REQUIRE(result.has_value());
    return result.value();
}

MappingIntent file_intent(
    std::uint64_t guest_base,
    std::uint64_t size,
    std::uint64_t file_offset,
    std::size_t source_index) {
    return MappingIntent{
        .range = range(guest_base, size),
        .permissions = read_only(),
        .backing =
            MappingBacking{
                .kind = MappingBackingKind::file,
                .file_offset = file_offset,
                .byte_count = GuestSize{size},
            },
        .source_index = source_index,
    };
}

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 2; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

void write_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

void write_symbol(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t name,
    std::uint8_t info,
    std::uint8_t other,
    std::uint16_t shndx,
    std::uint64_t value,
    std::uint64_t size) {
    write_u32(bytes, offset, name);
    bytes[offset + 4] = static_cast<std::byte>(info);
    bytes[offset + 5] = static_cast<std::byte>(other);
    write_u16(bytes, offset + 6, shndx);
    write_u64(bytes, offset + 8, value);
    write_u64(bytes, offset + 16, size);
}

void write_hash_header(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t nbucket,
    std::uint32_t nchain) {
    write_u32(bytes, offset, nbucket);
    write_u32(bytes, offset + 4, nchain);
}

}  // namespace

TEST_CASE("dynamic symbol metadata may be absent", "[loader][dynamic-symbols]") {
    std::array<std::byte, 0> image{};
    std::array<MappingIntent, 0> intents{};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({entry(0, 0, 0)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
}

TEST_CASE("DT_SYMTABSZ bounds a dynamic symbol table", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(2 * kSymSize, std::byte{0});
    write_symbol(image, kSymSize, 7, 0x12, 0x05, 3, 0x1234, 0x20);

    std::array intents{file_intent(0x1000, image.size(), 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0x1000, 1),
            entry(kDtSyment, kSymSize, 2),
            entry(kDtSymtabsz, 2 * kSymSize, 3),
        }),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().symbol_count == 2);
    REQUIRE(result->value().count_from_symtabsz);
    REQUIRE_FALSE(result->value().count_from_sysv_hash);

    auto symbol = astraea::loader::parse_dynamic_symbol(
        result->value(), 1, view.value());
    REQUIRE(symbol.has_value());
    REQUIRE(symbol->name_offset == 7);
    REQUIRE(symbol->info == 0x12);
    REQUIRE(symbol->other == 0x05);
    REQUIRE(symbol->section_index_raw == 3);
    REQUIRE(symbol->value == 0x1234);
    REQUIRE(symbol->size == 0x20);
}

TEST_CASE("SysV nchain bounds a dynamic symbol table", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(2 * kSymSize + 8, std::byte{0});
    write_hash_header(image, 2 * kSymSize, 1, 2);

    std::array intents{
        file_intent(0x2000, 2 * kSymSize, 0, 0),
        file_intent(0x3000, 8, 2 * kSymSize, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0x2000, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtHash, 0x3000, 2),
        }),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().symbol_count == 2);
    REQUIRE_FALSE(result->value().count_from_symtabsz);
    REQUIRE(result->value().count_from_sysv_hash);
}

TEST_CASE("symbol count sources must agree", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(2 * kSymSize + 8, std::byte{0});
    write_hash_header(image, 2 * kSymSize, 1, 2);
    std::array intents{
        file_intent(0x4000, 2 * kSymSize, 0, 0),
        file_intent(0x5000, 8, 2 * kSymSize, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    SECTION("agreement") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSymtab, 0x4000, 0),
                entry(kDtSyment, kSymSize, 1),
                entry(kDtSymtabsz, 2 * kSymSize, 2),
                entry(kDtHash, 0x5000, 3),
            }),
            view.value());

        REQUIRE(result.has_value());
        REQUIRE(result->has_value());
        REQUIRE(result->value().count_from_symtabsz);
        REQUIRE(result->value().count_from_sysv_hash);
    }

    SECTION("conflict") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSymtab, 0x4000, 0),
                entry(kDtSyment, kSymSize, 1),
                entry(kDtSymtabsz, kSymSize, 2),
                entry(kDtHash, 0x5000, 3),
            }),
            view.value());

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == DynamicSymbolErrorCode::conflicting_symbol_count);
    }
}

TEST_CASE("dynamic symbol singleton metadata rejects contradictions", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    std::array intents{file_intent(0x6000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0x6000, 0),
            entry(kDtSymtab, 0x7000, 1),
            entry(kDtSyment, kSymSize, 2),
            entry(kDtSymtabsz, kSymSize, 3),
        }),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicSymbolErrorCode::conflicting_dynamic_tag);
    REQUIRE(result.error().tag == kDtSymtab);
    REQUIRE(result.error().source_entry_index == std::size_t{1});
    REQUIRE(result.error().conflicting_entry_index == std::size_t{0});
}

TEST_CASE("dynamic symbol metadata requires SYMTAB and SYMENT", "[loader][dynamic-symbols]") {
    std::array<std::byte, 0> image{};
    std::array<MappingIntent, 0> intents{};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    SECTION("missing SYMTAB") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSyment, kSymSize, 0),
                entry(kDtSymtabsz, kSymSize, 1),
            }),
            view.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicSymbolErrorCode::missing_required_companion_tag);
        REQUIRE(result.error().tag == kDtSymtab);
    }

    SECTION("missing SYMENT") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSymtab, 0x1000, 0),
                entry(kDtSymtabsz, kSymSize, 1),
            }),
            view.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicSymbolErrorCode::missing_required_companion_tag);
        REQUIRE(result.error().tag == kDtSyment);
    }
}

TEST_CASE("dynamic symbol entry and table sizes are exact", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    std::array intents{file_intent(0x7000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    SECTION("invalid SYMENT") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSymtab, 0x7000, 0),
                entry(kDtSyment, 16, 1),
                entry(kDtSymtabsz, kSymSize, 2),
            }),
            view.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == DynamicSymbolErrorCode::invalid_symbol_entry_size);
    }

    SECTION("zero SYMTABSZ") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSymtab, 0x7000, 0),
                entry(kDtSyment, kSymSize, 1),
                entry(kDtSymtabsz, 0, 2),
            }),
            view.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == DynamicSymbolErrorCode::invalid_symbol_table_size);
    }

    SECTION("non-multiple SYMTABSZ") {
        auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
            table({
                entry(kDtSymtab, 0x7000, 0),
                entry(kDtSyment, kSymSize, 1),
                entry(kDtSymtabsz, kSymSize + 1, 2),
            }),
            view.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == DynamicSymbolErrorCode::invalid_symbol_table_size);
    }
}

TEST_CASE("symbol count is never guessed from surrounding mappings", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    std::array intents{file_intent(0x8000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0x8000, 0),
            entry(kDtSyment, kSymSize, 1),
        }),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicSymbolErrorCode::symbol_count_unavailable);
}

TEST_CASE("symbol table guest range overflow is rejected", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    std::array intents{file_intent(0x9000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    const auto max = std::numeric_limits<std::uint64_t>::max();
    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, max - 7U, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtSymtabsz, kSymSize, 2),
        }),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicSymbolErrorCode::symbol_table_range_overflow);
}

TEST_CASE("reserved symbol zero must be the generic undefined record", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(kSymSize, std::byte{0});
    write_symbol(image, 0, 1, 0, 0, 0, 0, 0);
    std::array intents{file_intent(0xa000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0xa000, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtSymtabsz, kSymSize, 2),
        }),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicSymbolErrorCode::invalid_undefined_symbol);
}

TEST_CASE("symbol parsing enforces bounds and generic field helpers", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(2 * kSymSize, std::byte{0});
    write_symbol(image, kSymSize, 9, 0xab, 0xe5, 0xffffU, 0x11223344, 0x55667788);
    std::array intents{file_intent(0xb000, image.size(), 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto descriptor = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0xb000, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtSymtabsz, 2 * kSymSize, 2),
        }),
        view.value());
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());

    auto symbol = astraea::loader::parse_dynamic_symbol(
        descriptor->value(), 1, view.value());
    REQUIRE(symbol.has_value());
    REQUIRE(symbol->name_offset == 9);
    REQUIRE(symbol->binding() == 0x0a);
    REQUIRE(symbol->type() == 0x0b);
    REQUIRE(symbol->visibility() == 0x05);
    REQUIRE(symbol->is_extended_section_index());

    auto past_end = astraea::loader::parse_dynamic_symbol(
        descriptor->value(), 2, view.value());
    REQUIRE_FALSE(past_end.has_value());
    REQUIRE(past_end.error().code == DynamicSymbolErrorCode::symbol_index_out_of_bounds);
}

TEST_CASE("symbol records may cross compatible mapping boundaries", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(2 * kSymSize, std::byte{0});
    write_symbol(image, kSymSize, 4, 0x11, 0, 1, 7, 8);

    std::array intents{
        file_intent(0xc000, kSymSize + 12, 0, 0),
        file_intent(0xc000 + kSymSize + 12, 12, kSymSize + 12, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto descriptor = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0xc000, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtSymtabsz, 2 * kSymSize, 2),
        }),
        view.value());
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());

    auto symbol = astraea::loader::parse_dynamic_symbol(
        descriptor->value(), 1, view.value());
    REQUIRE(symbol.has_value());
    REQUIRE(symbol->name_offset == 4);
    REQUIRE(symbol->value == 7);
    REQUIRE(symbol->size == 8);
}

TEST_CASE("unreadable symbol preserves initialized-image failure", "[loader][dynamic-symbols]") {
    std::vector<std::byte> image(kSymSize, std::byte{0});
    std::array intents{file_intent(0xd000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto descriptor = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0xd000, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtSymtabsz, 2 * kSymSize, 2),
        }),
        view.value());
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());

    auto symbol = astraea::loader::parse_dynamic_symbol(
        descriptor->value(), 1, view.value());
    REQUIRE_FALSE(symbol.has_value());
    REQUIRE(symbol.error().code == DynamicSymbolErrorCode::symbol_entry_unreadable);
    REQUIRE(symbol.error().image_error.has_value());
    REQUIRE(
        symbol.error().image_error->code ==
        astraea::memory::InitializedImageErrorCode::unmapped_guest_address);
}

TEST_CASE("one-symbol table may end at UINT64_MAX", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    const auto max = std::numeric_limits<std::uint64_t>::max();
    std::array intents{file_intent(max - (kSymSize - 1U), kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto descriptor = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, max - (kSymSize - 1U), 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtSymtabsz, kSymSize, 2),
        }),
        view.value());

    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());
    REQUIRE(descriptor->value().range.contains(GuestAddress{max}));
}


TEST_CASE("identical dynamic symbol singleton tags are accepted", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    std::array intents{file_intent(0xe000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0xe000, 0),
            entry(kDtSymtab, 0xe000, 1),
            entry(kDtSyment, kSymSize, 2),
            entry(kDtSyment, kSymSize, 3),
            entry(kDtSymtabsz, kSymSize, 4),
            entry(kDtSymtabsz, kSymSize, 5),
        }),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().symbol_count == 1);
    REQUIRE(result->value().symtab_source_entry_index == 0);
    REQUIRE(result->value().syment_source_entry_index == 2);
    REQUIRE(result->value().symtabsz_source_entry_index == std::size_t{4});
}

TEST_CASE("dynamic symbol descriptor preserves SysV hash failures", "[loader][dynamic-symbols]") {
    std::array<std::byte, kSymSize> image{};
    std::array intents{file_intent(0xf000, kSymSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_dynamic_symbol_table_descriptor(
        table({
            entry(kDtSymtab, 0xf000, 0),
            entry(kDtSyment, kSymSize, 1),
            entry(kDtHash, 0x11000, 2),
        }),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicSymbolErrorCode::sysv_hash_failure);
    REQUIRE(result.error().sysv_hash_error.has_value());
    REQUIRE(
        result.error().sysv_hash_error->code ==
        astraea::loader::SysvHashErrorCode::hash_header_unreadable);
    REQUIRE(result.error().sysv_hash_error->image_error.has_value());
}
