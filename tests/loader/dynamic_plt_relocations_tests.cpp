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

constexpr std::int64_t kDtPltrelsz = 2;
constexpr std::int64_t kDtRela = 7;
constexpr std::int64_t kDtRel = 17;
constexpr std::int64_t kDtPltrel = 20;
constexpr std::int64_t kDtJmprel = 23;
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

void write_rela(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t target,
    std::uint32_t symbol,
    std::uint32_t type,
    std::int64_t addend) {
    write_u64(bytes, offset, target);
    const auto info =
        (static_cast<std::uint64_t>(symbol) << 32U) |
        static_cast<std::uint64_t>(type);
    write_u64(bytes, offset + 8, info);
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

TEST_CASE("PLT relocation metadata may be absent", "[loader][plt-relocations]") {
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({entry(0, 0, 0)}));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
}

TEST_CASE("DT_PLTREL selects REL encoding", "[loader][plt-relocations]") {
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, 0x1000, 0),
            entry(kDtPltrelsz, 2 * kRelSize, 1),
            entry(kDtPltrel, static_cast<std::uint64_t>(kDtRel), 2),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().kind == RelocationTableKind::plt_rel);
    REQUIRE(result->value().entry_size == GuestSize{kRelSize});
    REQUIRE(result->value().count == 2);
}

TEST_CASE("DT_PLTREL selects RELA encoding", "[loader][plt-relocations]") {
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, 0x2000, 0),
            entry(kDtPltrelsz, kRelaSize, 1),
            entry(kDtPltrel, static_cast<std::uint64_t>(kDtRela), 2),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().kind == RelocationTableKind::plt_rela);
    REQUIRE(result->value().entry_size == GuestSize{kRelaSize});
    REQUIRE(result->value().count == 1);
}

TEST_CASE("PLT relocation group requires all companions", "[loader][plt-relocations]") {
    SECTION("missing JMPREL") {
        auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
            table({
                entry(kDtPltrelsz, kRelSize, 0),
                entry(kDtPltrel, static_cast<std::uint64_t>(kDtRel), 1),
            }));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            DynamicRelocationErrorCode::missing_required_companion_tag);
        REQUIRE(result.error().tag == kDtJmprel);
    }

    SECTION("missing PLTRELSZ") {
        auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
            table({
                entry(kDtJmprel, 0x1000, 0),
                entry(kDtPltrel, static_cast<std::uint64_t>(kDtRel), 1),
            }));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().tag == kDtPltrelsz);
    }

    SECTION("missing PLTREL") {
        auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
            table({
                entry(kDtJmprel, 0x1000, 0),
                entry(kDtPltrelsz, kRelSize, 1),
            }));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().tag == kDtPltrel);
    }
}

TEST_CASE("invalid DT_PLTREL encoding is rejected", "[loader][plt-relocations]") {
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, 0x1000, 0),
            entry(kDtPltrelsz, kRelSize, 1),
            entry(kDtPltrel, 1234, 2),
        }));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::invalid_plt_relocation_encoding);
    REQUIRE(result.error().source_entry_index == std::size_t{2});
}

TEST_CASE("PLT relocation size must divide selected width", "[loader][plt-relocations]") {
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, 0x1000, 0),
            entry(kDtPltrelsz, kRelaSize + 1, 1),
            entry(kDtPltrel, static_cast<std::uint64_t>(kDtRela), 2),
        }));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::invalid_relocation_table_size);
}

TEST_CASE("empty PLT table may live at final guest address", "[loader][plt-relocations]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, max, 0),
            entry(kDtPltrelsz, 0, 1),
            entry(kDtPltrel, static_cast<std::uint64_t>(kDtRel), 2),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().range.empty());
    REQUIRE(result->value().range.base() == GuestAddress{max});
}

TEST_CASE("PLT singleton contradictions fail deterministically", "[loader][plt-relocations]") {
    auto result = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, 0x1000, 2),
            entry(kDtJmprel, 0x2000, 7),
            entry(kDtPltrelsz, kRelSize, 8),
            entry(kDtPltrel, static_cast<std::uint64_t>(kDtRel), 9),
        }));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        DynamicRelocationErrorCode::conflicting_dynamic_tag);
    REQUIRE(result.error().tag == kDtJmprel);
    REQUIRE(result.error().source_entry_index == std::size_t{7});
    REQUIRE(result.error().conflicting_entry_index == std::size_t{2});
}

TEST_CASE("PLT and general relocation tables remain independent", "[loader][plt-relocations]") {
    auto dynamic = table({
        entry(kDtRel, 0x3000, 0),
        entry(kDtRelsz, kRelSize, 1),
        entry(kDtRelent, kRelSize, 2),
        entry(kDtJmprel, 0x3000, 3),
        entry(kDtPltrelsz, kRelSize, 4),
        entry(kDtPltrel, static_cast<std::uint64_t>(kDtRel), 5),
    });

    auto general =
        astraea::loader::build_general_dynamic_relocation_metadata(dynamic);
    auto plt = astraea::loader::build_plt_dynamic_relocation_metadata(dynamic);

    REQUIRE(general.has_value());
    REQUIRE(general->rel.has_value());
    REQUIRE(plt.has_value());
    REQUIRE(plt->has_value());
    REQUIRE(general->rel->range == plt->value().range);
    REQUIRE(general->rel->kind == RelocationTableKind::rel);
    REQUIRE(plt->value().kind == RelocationTableKind::plt_rel);
}

TEST_CASE("PLT RELA reuses neutral relocation decoder", "[loader][plt-relocations]") {
    std::vector<std::byte> image(kRelaSize, std::byte{0});
    write_rela(image, 0, 0x4444, 1, 7, -12);
    std::array intents{file_intent(0x4000, kRelaSize, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto descriptor = astraea::loader::build_plt_dynamic_relocation_metadata(
        table({
            entry(kDtJmprel, 0x4000, 0),
            entry(kDtPltrelsz, kRelaSize, 1),
            entry(kDtPltrel, static_cast<std::uint64_t>(kDtRela), 2),
        }));
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());

    auto relocation = astraea::loader::parse_dynamic_relocation(
        descriptor->value(), 0, symbols(2), view.value());

    REQUIRE(relocation.has_value());
    REQUIRE(relocation->table_kind == RelocationTableKind::plt_rela);
    REQUIRE(relocation->target == GuestAddress{0x4444});
    REQUIRE(relocation->symbol_index == 1);
    REQUIRE(relocation->relocation_type == 7);
    REQUIRE(relocation->addend == std::optional<std::int64_t>{-12});
}
