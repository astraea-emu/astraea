#include <astraea/loader/dynamic_relocations.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::DynamicEntry;
using astraea::loader::DynamicRelocationErrorCode;
using astraea::loader::DynamicRelocationTableDescriptor;
using astraea::loader::DynamicSymbolTableDescriptor;
using astraea::loader::DynamicTable;
using astraea::loader::RelocationTableKind;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::InitializedImageView;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

constexpr std::int64_t kDtRela = 7;
constexpr std::int64_t kDtRelasz = 8;
constexpr std::int64_t kDtRelaent = 9;
constexpr std::int64_t kDtRel = 17;
constexpr std::int64_t kDtRelsz = 18;
constexpr std::int64_t kDtRelent = 19;

constexpr std::uint64_t kRelSize = 16;
constexpr std::uint64_t kRelaSize = 24;

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

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

void write_rel(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t target,
    std::uint32_t symbol,
    std::uint32_t type) {
    write_u64(bytes, offset, target);
    const auto info =
        (static_cast<std::uint64_t>(symbol) << 32U) |
        static_cast<std::uint64_t>(type);
    write_u64(bytes, offset + 8, info);
}

void write_rela(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t target,
    std::uint32_t symbol,
    std::uint32_t type,
    std::int64_t addend) {
    write_rel(bytes, offset, target, symbol, type);
    write_u64(bytes, offset + 16, std::bit_cast<std::uint64_t>(addend));
}

DynamicSymbolTableDescriptor symbols(std::uint64_t count) {
    auto symbol_range = GuestRange::create(
        GuestAddress{0x100},
        GuestSize{count == 0 ? std::uint64_t{0} : std::uint64_t{24}});
    REQUIRE(symbol_range.has_value());

    return DynamicSymbolTableDescriptor{
        .range = symbol_range.value(),
        .entry_size = GuestSize{24},
        .symbol_count = count,
        .count_from_symtabsz = true,
        .count_from_sysv_hash = false,
        .symtab_source_entry_index = 0,
        .syment_source_entry_index = 1,
        .symtabsz_source_entry_index = 2,
        .hash_source_entry_index = std::nullopt,
    };
}

}  // namespace

TEST_CASE("general relocation metadata may be absent", "[loader][relocations]") {
    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({entry(0, 0, 0)}));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->rel.has_value());
    REQUIRE_FALSE(result->rela.has_value());
}

TEST_CASE("REL descriptor requires exact companions and width", "[loader][relocations]") {
    SECTION("valid empty REL table") {
        auto result = astraea::loader::build_general_dynamic_relocation_metadata(
            table({
                entry(kDtRel, std::numeric_limits<std::uint64_t>::max(), 0),
                entry(kDtRelsz, 0, 1),
                entry(kDtRelent, kRelSize, 2),
            }));

        REQUIRE(result.has_value());
        REQUIRE(result->rel.has_value());
        REQUIRE(result->rel->count == 0);
        REQUIRE(result->rel->range.empty());
        REQUIRE(result->rel->range.base() ==
                GuestAddress{std::numeric_limits<std::uint64_t>::max()});
    }

    SECTION("missing RELSZ") {
        auto result = astraea::loader::build_general_dynamic_relocation_metadata(
            table({
                entry(kDtRel, 0x1000, 0),
                entry(kDtRelent, kRelSize, 1),
            }));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicRelocationErrorCode::missing_required_companion_tag);
        REQUIRE(result.error().tag == kDtRelsz);
    }

    SECTION("wrong RELENT") {
        auto result = astraea::loader::build_general_dynamic_relocation_metadata(
            table({
                entry(kDtRel, 0x1000, 0),
                entry(kDtRelsz, kRelSize, 1),
                entry(kDtRelent, 24, 2),
            }));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicRelocationErrorCode::invalid_relocation_entry_size);
    }

    SECTION("RELSZ must divide exactly") {
        auto result = astraea::loader::build_general_dynamic_relocation_metadata(
            table({
                entry(kDtRel, 0x1000, 0),
                entry(kDtRelsz, kRelSize + 1, 1),
                entry(kDtRelent, kRelSize, 2),
            }));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicRelocationErrorCode::invalid_relocation_table_size);
    }
}

TEST_CASE("RELA descriptor requires exact companions and width", "[loader][relocations]") {
    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRela, 0x2000, 0),
            entry(kDtRelasz, 2 * kRelaSize, 1),
            entry(kDtRelaent, kRelaSize, 2),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->rela.has_value());
    REQUIRE(result->rela->kind == RelocationTableKind::rela);
    REQUIRE(result->rela->count == 2);
    REQUIRE(result->rela->range.base() == GuestAddress{0x2000});
    REQUIRE(result->rela->range.size() == GuestSize{2 * kRelaSize});
}

TEST_CASE("general REL and RELA descriptors coexist independently", "[loader][relocations]") {
    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, 0x3000, 0),
            entry(kDtRelsz, kRelSize, 1),
            entry(kDtRelent, kRelSize, 2),
            entry(kDtRela, 0x4000, 3),
            entry(kDtRelasz, kRelaSize, 4),
            entry(kDtRelaent, kRelaSize, 5),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->rel.has_value());
    REQUIRE(result->rela.has_value());
    REQUIRE(result->rel->kind == RelocationTableKind::rel);
    REQUIRE(result->rela->kind == RelocationTableKind::rela);
}

TEST_CASE("relocation singleton contradictions fail deterministically", "[loader][relocations]") {
    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, 0x1000, 2),
            entry(kDtRel, 0x2000, 7),
            entry(kDtRelsz, kRelSize, 8),
            entry(kDtRelent, kRelSize, 9),
        }));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::conflicting_dynamic_tag);
    REQUIRE(result.error().tag == kDtRel);
    REQUIRE(result.error().source_entry_index == std::size_t{7});
    REQUIRE(result.error().conflicting_entry_index == std::size_t{2});
}

TEST_CASE("identical relocation singleton duplicates are accepted", "[loader][relocations]") {
    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, 0x5000, 0),
            entry(kDtRel, 0x5000, 4),
            entry(kDtRelsz, kRelSize, 1),
            entry(kDtRelsz, kRelSize, 5),
            entry(kDtRelent, kRelSize, 2),
            entry(kDtRelent, kRelSize, 6),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->rel.has_value());
    REQUIRE(result->rel->address_source_entry_index == 0);
    REQUIRE(result->rel->size_source_entry_index == 1);
    REQUIRE(result->rel->encoding_source_entry_index == 2);
}

TEST_CASE("relocation table range overflow is rejected", "[loader][relocations]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRela, max - 7U, 0),
            entry(kDtRelasz, kRelaSize, 1),
            entry(kDtRelaent, kRelaSize, 2),
        }));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::relocation_table_range_overflow);
}

TEST_CASE("one REL record may end at UINT64_MAX", "[loader][relocations]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto result = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, max - (kRelSize - 1U), 0),
            entry(kDtRelsz, kRelSize, 1),
            entry(kDtRelent, kRelSize, 2),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->rel.has_value());
    REQUIRE(result->rel->range.contains(GuestAddress{max}));
}

TEST_CASE("REL decoding preserves raw info and has no addend", "[loader][relocations]") {
    std::vector<std::byte> image(kRelSize, std::byte{0});
    write_rel(image, 0, 0xdeadbeefULL, 2, 0xfedcba98U);
    std::array intents{file_intent(0x6000, kRelSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto metadata = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, 0x6000, 0),
            entry(kDtRelsz, kRelSize, 1),
            entry(kDtRelent, kRelSize, 2),
        }));
    REQUIRE(metadata.has_value());
    REQUIRE(metadata->rel.has_value());

    auto result = astraea::loader::parse_dynamic_relocation(
        metadata->rel.value(), 0, symbols(3), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->table_kind == RelocationTableKind::rel);
    REQUIRE(result->table_index == 0);
    REQUIRE(result->target == GuestAddress{0xdeadbeefULL});
    REQUIRE(result->symbol_index == 2);
    REQUIRE(result->relocation_type == 0xfedcba98U);
    REQUIRE(
        result->raw_info ==
        ((std::uint64_t{2} << 32U) | std::uint64_t{0xfedcba98U}));
    REQUIRE_FALSE(result->addend.has_value());
}

TEST_CASE("RELA decoding preserves signed addend bit patterns", "[loader][relocations]") {
    const std::array<std::int64_t, 4> addends{
        std::int64_t{0},
        std::int64_t{123},
        std::int64_t{-7},
        std::numeric_limits<std::int64_t>::min(),
    };

    for (const auto addend : addends) {
        DYNAMIC_SECTION("addend " << addend) {
            std::vector<std::byte> image(kRelaSize, std::byte{0});
            write_rela(
                image,
                0,
                std::numeric_limits<std::uint64_t>::max(),
                0,
                0xffffffffU,
                addend);
            std::array intents{file_intent(0x7000, kRelaSize, 0, 0)};
            auto view = InitializedImageView::create(image, intents);
            REQUIRE(view.has_value());

            auto metadata =
                astraea::loader::build_general_dynamic_relocation_metadata(
                    table({
                        entry(kDtRela, 0x7000, 0),
                        entry(kDtRelasz, kRelaSize, 1),
                        entry(kDtRelaent, kRelaSize, 2),
                    }));
            REQUIRE(metadata.has_value());
            REQUIRE(metadata->rela.has_value());

            auto result = astraea::loader::parse_dynamic_relocation(
                metadata->rela.value(), 0, symbols(1), view.value());

            REQUIRE(result.has_value());
            REQUIRE(result->target ==
                    GuestAddress{std::numeric_limits<std::uint64_t>::max()});
            REQUIRE(result->relocation_type == 0xffffffffU);
            REQUIRE(result->addend.has_value());
            REQUIRE(result->addend.value() == addend);
        }
    }
}

TEST_CASE("relocation symbol indexes are bounded by dynamic symbols", "[loader][relocations]") {
    std::vector<std::byte> image(kRelSize, std::byte{0});

    SECTION("index zero is valid") {
        write_rel(image, 0, 0x100, 0, 8);
        std::array intents{file_intent(0x8000, kRelSize, 0, 0)};
        auto view = InitializedImageView::create(image, intents);
        REQUIRE(view.has_value());

        auto metadata =
            astraea::loader::build_general_dynamic_relocation_metadata(
                table({
                    entry(kDtRel, 0x8000, 0),
                    entry(kDtRelsz, kRelSize, 1),
                    entry(kDtRelent, kRelSize, 2),
                }));
        REQUIRE(metadata.has_value());

        auto result = astraea::loader::parse_dynamic_relocation(
            metadata->rel.value(), 0, symbols(1), view.value());
        REQUIRE(result.has_value());
        REQUIRE(result->symbol_index == 0);
    }

    SECTION("index equal to count is rejected") {
        write_rel(image, 0, 0x100, 2, 8);
        std::array intents{file_intent(0x8000, kRelSize, 0, 0)};
        auto view = InitializedImageView::create(image, intents);
        REQUIRE(view.has_value());

        auto metadata =
            astraea::loader::build_general_dynamic_relocation_metadata(
                table({
                    entry(kDtRel, 0x8000, 0),
                    entry(kDtRelsz, kRelSize, 1),
                    entry(kDtRelent, kRelSize, 2),
                }));
        REQUIRE(metadata.has_value());

        auto result = astraea::loader::parse_dynamic_relocation(
            metadata->rel.value(), 0, symbols(2), view.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicRelocationErrorCode::relocation_symbol_index_out_of_bounds);
        REQUIRE(result.error().symbol_index == std::uint32_t{2});
    }
}

TEST_CASE("relocation record index is bounded before reading", "[loader][relocations]") {
    std::vector<std::byte> image(kRelSize, std::byte{0});
    std::array intents{file_intent(0x9000, kRelSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto metadata = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, 0x9000, 0),
            entry(kDtRelsz, kRelSize, 1),
            entry(kDtRelent, kRelSize, 2),
        }));
    REQUIRE(metadata.has_value());

    auto result = astraea::loader::parse_dynamic_relocation(
        metadata->rel.value(), 1, symbols(1), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::relocation_index_out_of_bounds);
    REQUIRE(result.error().table_index == std::uint64_t{1});
}

TEST_CASE("relocation records may cross compatible mapping boundaries", "[loader][relocations]") {
    std::vector<std::byte> image(kRelaSize, std::byte{0});
    write_rela(image, 0, 0x1234, 1, 6, -9);

    std::array intents{
        file_intent(0xa000, 10, 0, 0),
        file_intent(0xa00a, kRelaSize - 10, 10, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto metadata = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRela, 0xa000, 0),
            entry(kDtRelasz, kRelaSize, 1),
            entry(kDtRelaent, kRelaSize, 2),
        }));
    REQUIRE(metadata.has_value());

    auto result = astraea::loader::parse_dynamic_relocation(
        metadata->rela.value(), 0, symbols(2), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->target == GuestAddress{0x1234});
    REQUIRE(result->symbol_index == 1);
    REQUIRE(result->relocation_type == 6);
    REQUIRE(result->addend == std::optional<std::int64_t>{-9});
}

TEST_CASE("unreadable relocation preserves initialized-image failure", "[loader][relocations]") {
    std::vector<std::byte> image(8, std::byte{0});
    std::array intents{file_intent(0xb000, 8, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto metadata = astraea::loader::build_general_dynamic_relocation_metadata(
        table({
            entry(kDtRel, 0xb000, 0),
            entry(kDtRelsz, kRelSize, 1),
            entry(kDtRelent, kRelSize, 2),
        }));
    REQUIRE(metadata.has_value());

    auto result = astraea::loader::parse_dynamic_relocation(
        metadata->rel.value(), 0, symbols(1), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::relocation_entry_unreadable);
    REQUIRE(result.error().image_error.has_value());
    REQUIRE(
        result.error().image_error->code ==
        astraea::memory::InitializedImageErrorCode::unmapped_guest_address);
    REQUIRE(result.error().guest_address == GuestAddress{0xb008});
}

TEST_CASE("decoder defensively rejects descriptor width mismatch", "[loader][relocations]") {
    auto descriptor_range = GuestRange::create(GuestAddress{0xc000}, GuestSize{kRelSize});
    REQUIRE(descriptor_range.has_value());

    DynamicRelocationTableDescriptor descriptor{
        .kind = RelocationTableKind::rel,
        .range = descriptor_range.value(),
        .entry_size = GuestSize{kRelaSize},
        .count = 1,
        .address_source_entry_index = 0,
        .size_source_entry_index = 1,
        .encoding_source_entry_index = 2,
    };

    std::array<std::byte, kRelSize> image{};
    std::array intents{file_intent(0xc000, kRelSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::parse_dynamic_relocation(
        descriptor, 0, symbols(1), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::invalid_relocation_entry_size);
}
