#include <astraea/loader/sysv_hash.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::DynamicEntry;
using astraea::loader::DynamicTable;
using astraea::loader::SysvHashErrorCode;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::InitializedImageView;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

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

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

std::vector<std::byte> hash_header(std::uint32_t nbucket, std::uint32_t nchain) {
    std::vector<std::byte> bytes(8);
    write_u32(bytes, 0, nbucket);
    write_u32(bytes, 4, nchain);
    return bytes;
}

}  // namespace

TEST_CASE("SysV hash metadata may be absent", "[loader][sysv-hash]") {
    std::array<MappingIntent, 0> intents{};
    std::array<std::byte, 0> image{};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(0, 0, 0)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
}

TEST_CASE("SysV hash header yields bucket and symbol counts", "[loader][sysv-hash]") {
    auto image = hash_header(7, 11);
    std::array intents{file_intent(0x1000, 8, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x1000, 3), entry(0, 0, 4)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().hash_address == GuestAddress{0x1000});
    REQUIRE(result->value().bucket_count == 7);
    REQUIRE(result->value().symbol_count == 11);
    REQUIRE(result->value().source_entry_index == 3);
}

TEST_CASE("identical duplicate DT_HASH values are accepted", "[loader][sysv-hash]") {
    auto image = hash_header(1, 2);
    std::array intents{file_intent(0x2000, 8, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x2000, 1), entry(4, 0x2000, 5)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().source_entry_index == 1);
}

TEST_CASE("conflicting duplicate DT_HASH values fail deterministically", "[loader][sysv-hash]") {
    std::array<MappingIntent, 0> intents{};
    std::array<std::byte, 0> image{};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x1000, 2), entry(4, 0x2000, 7)}),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == SysvHashErrorCode::conflicting_dynamic_tag);
    REQUIRE(result.error().source_entry_index == std::size_t{7});
    REQUIRE(result.error().conflicting_entry_index == std::size_t{2});
}

TEST_CASE("SysV hash requires at least reserved symbol zero", "[loader][sysv-hash]") {
    auto image = hash_header(1, 0);
    std::array intents{file_intent(0x3000, 8, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x3000, 9)}),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == SysvHashErrorCode::invalid_symbol_count);
    REQUIRE(result.error().source_entry_index == std::size_t{9});
}

TEST_CASE("SysV hash header may cross compatible mapping boundaries", "[loader][sysv-hash]") {
    auto image = hash_header(3, 5);
    std::array intents{
        file_intent(0x4000, 4, 0, 0),
        file_intent(0x4004, 4, 4, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x4000, 0)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().bucket_count == 3);
    REQUIRE(result->value().symbol_count == 5);
}

TEST_CASE("unmapped SysV hash header preserves image failure", "[loader][sysv-hash]") {
    std::vector<std::byte> image(4);
    std::array intents{file_intent(0x5000, 4, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x5000, 6)}),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == SysvHashErrorCode::hash_header_unreadable);
    REQUIRE(result.error().image_error.has_value());
    REQUIRE(
        result.error().image_error->code ==
        astraea::memory::InitializedImageErrorCode::unmapped_guest_address);
    REQUIRE(result.error().guest_address == GuestAddress{0x5004});
}

TEST_CASE("SysV hash header supports final uint64 guest byte", "[loader][sysv-hash]") {
    auto image = hash_header(1, 1);
    const auto max = std::numeric_limits<std::uint64_t>::max();
    std::array intents{file_intent(max - 7U, 8, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, max - 7U, 0)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().symbol_count == 1);
}

TEST_CASE("SysV hash header guest range overflow is rejected", "[loader][sysv-hash]") {
    std::array<MappingIntent, 0> intents{};
    std::array<std::byte, 0> image{};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    const auto max = std::numeric_limits<std::uint64_t>::max();
    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, max - 3U, 0)}),
        view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == SysvHashErrorCode::hash_header_range_overflow);
}

TEST_CASE("huge SysV bucket count does not require hash body", "[loader][sysv-hash]") {
    auto image =
        hash_header(std::numeric_limits<std::uint32_t>::max(), 2);
    std::array intents{file_intent(0x6000, 8, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result = astraea::loader::build_sysv_hash_count_evidence(
        table({entry(4, 0x6000, 0)}),
        view.value());

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(
        result->value().bucket_count ==
        std::numeric_limits<std::uint32_t>::max());
    REQUIRE(result->value().symbol_count == 2);
}
