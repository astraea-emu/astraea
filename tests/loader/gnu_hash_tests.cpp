#include <astraea/loader/gnu_hash.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::DynamicEntry;
using astraea::loader::DynamicTable;
using astraea::loader::GnuHashErrorCode;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::InitializedImageView;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

constexpr std::int64_t kDtGnuHash = 0x6ffffef5;
constexpr std::uint64_t kBase = 0x8000U;

void write_u32(std::vector<std::byte>& data, std::size_t offset,
               std::uint32_t value) {
    for (std::size_t i = 0U; i < 4U; ++i)
        data[offset + i] = static_cast<std::byte>(
            (value >> (i * 8U)) & 0xffU);
}

std::vector<std::byte> two_bucket_hash() {
    // header: 2 buckets, first hashed symbol 1, 1 bloom word, shift 5
    // bucket 0 starts symbol 1; bucket 1 starts symbol 4
    // chains 1,2,3,4: the final bucket's last entry is chain[3] (odd)
    std::vector<std::byte> bytes(16U + 8U + 8U + 16U);
    write_u32(bytes, 0U, 2U);
    write_u32(bytes, 4U, 1U);
    write_u32(bytes, 8U, 1U);
    write_u32(bytes, 12U, 5U);
    write_u32(bytes, 24U, 1U);
    write_u32(bytes, 28U, 4U);
    write_u32(bytes, 32U, 2U);
    write_u32(bytes, 36U, 4U);
    write_u32(bytes, 40U, 5U);
    write_u32(bytes, 44U, 7U);
    return bytes;
}

MappingIntent mapped(std::uint64_t base, std::uint64_t size) {
    auto range = GuestRange::create(GuestAddress{base}, GuestSize{size});
    REQUIRE(range.has_value());
    auto permissions = GuestPermissions::checked_from_bits(4U);
    REQUIRE(permissions.has_value());
    return MappingIntent{
        .range = range.value(),
        .permissions = permissions.value(),
        .backing = MappingBacking{
            .kind = MappingBackingKind::file,
            .file_offset = 0U,
            .byte_count = GuestSize{size},
        },
        .source_index = 0U,
    };
}

DynamicTable input(std::uint64_t address = kBase) {
    return DynamicTable{.entries = {
        DynamicEntry{.tag = kDtGnuHash, .value = address, .index = 3U},
        DynamicEntry{.tag = 0, .value = 0U, .index = 4U},
    }};
}

}  // namespace

TEST_CASE("GNU hash count evidence may be absent", "[loader][gnu-hash]") {
    std::array<std::byte, 0U> empty{};
    std::array<MappingIntent, 0U> intents{};
    auto view = InitializedImageView::create(empty, intents);
    REQUIRE(view.has_value());
    DynamicTable table{.entries = {}};
    const auto result =
        astraea::loader::build_gnu_hash_count_evidence(table, view.value());
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
}

TEST_CASE("GNU hash derives last dynamic symbol from final bucket chain",
          "[loader][gnu-hash][count]") {
    auto bytes = two_bucket_hash();
    std::array intents{mapped(kBase, bytes.size())};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());
    const auto result =
        astraea::loader::build_gnu_hash_count_evidence(input(), view.value());
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().bucket_count == 2U);
    REQUIRE(result->value().first_hashed_symbol == 1U);
    REQUIRE(result->value().symbol_count == 5U);
    REQUIRE(result->value().source_entry_index == 3U);
}

TEST_CASE("GNU hash with empty buckets counts only unhashed prefix",
          "[loader][gnu-hash][count]") {
    auto bytes = two_bucket_hash();
    write_u32(bytes, 4U, 3U);
    write_u32(bytes, 24U, 0U);
    write_u32(bytes, 28U, 0U);
    bytes.resize(32U);
    std::array intents{mapped(kBase, bytes.size())};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());
    auto result =
        astraea::loader::build_gnu_hash_count_evidence(input(), view.value());
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().symbol_count == 3U);
}

TEST_CASE("GNU hash identical duplicate tag is stable; conflicting tag fails",
          "[loader][gnu-hash][identity]") {
    auto bytes = two_bucket_hash();
    std::array intents{mapped(kBase, bytes.size())};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());
    auto table = input();
    table.entries.push_back(
        DynamicEntry{.tag = kDtGnuHash, .value = kBase, .index = 5U});
    REQUIRE(astraea::loader::build_gnu_hash_count_evidence(
        table, view.value()).has_value());
    table.entries.back().value = kBase + 4U;
    const auto result =
        astraea::loader::build_gnu_hash_count_evidence(table, view.value());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
            GnuHashErrorCode::conflicting_dynamic_tag);
    REQUIRE(result.error().source_entry_index == std::size_t{5U});
    REQUIRE(result.error().conflicting_entry_index == std::size_t{3U});
}

TEST_CASE("GNU hash refuses malformed header, bucket and chain",
          "[loader][gnu-hash][refusal]") {
    auto bytes = two_bucket_hash();
    std::array intents{mapped(kBase, bytes.size())};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());
    const auto fail_for = [&](std::size_t offset, std::uint32_t replacement,
                              GnuHashErrorCode expected) {
        auto mutated = bytes;
        write_u32(mutated, offset, replacement);
        auto current = InitializedImageView::create(mutated, intents);
        REQUIRE(current.has_value());
        auto result = astraea::loader::build_gnu_hash_count_evidence(
            input(), current.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == expected);
    };
    fail_for(0U, 0U, GnuHashErrorCode::invalid_hash_header);
    fail_for(0U, 65537U, GnuHashErrorCode::invalid_hash_header);
    fail_for(4U, 0U, GnuHashErrorCode::invalid_hash_header);
    fail_for(8U, 3U, GnuHashErrorCode::invalid_hash_header);
    fail_for(28U, 1000000U, GnuHashErrorCode::invalid_bucket);
}

TEST_CASE("GNU hash unreadable chain does not infer a symbol count",
          "[loader][gnu-hash][refusal]") {
    auto bytes = two_bucket_hash();
    // Last hashed word no longer carries a terminating low bit.
    write_u32(bytes, 44U, 6U);
    std::array intents{mapped(kBase, bytes.size())};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());
    auto result =
        astraea::loader::build_gnu_hash_count_evidence(input(), view.value());
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
            GnuHashErrorCode::hash_data_unreadable);
    REQUIRE(result.error().image_error.has_value());
}

TEST_CASE("GNU hash rejects unmapped input and wrapped address",
          "[loader][gnu-hash][refusal]") {
    auto bytes = two_bucket_hash();
    std::array<MappingIntent, 0U> absent{};
    auto view = InitializedImageView::create(bytes, absent);
    REQUIRE(view.has_value());
    auto unmapped =
        astraea::loader::build_gnu_hash_count_evidence(input(), view.value());
    REQUIRE_FALSE(unmapped.has_value());
    REQUIRE(unmapped.error().code ==
            GnuHashErrorCode::hash_data_unreadable);

    const auto max = std::numeric_limits<std::uint64_t>::max();
    auto overflow =
        astraea::loader::build_gnu_hash_count_evidence(
            input(max - 1U), view.value());
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(overflow.error().code ==
            GnuHashErrorCode::hash_range_overflow);
}
