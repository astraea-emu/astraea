#include <astraea/loader/dynamic.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kPtDynamic = 2;
constexpr std::size_t kEntrySize = 16;

void write_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

void write_dynamic_entry(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::uint64_t raw_tag,
    std::uint64_t value) {
    const auto offset = index * kEntrySize;
    write_u64(bytes, offset, raw_tag);
    write_u64(bytes, offset + 8, value);
}

astraea::loader::ProgramHeader dynamic_header(
    std::uint64_t offset,
    std::uint64_t file_size) {
    return astraea::loader::ProgramHeader{
        .type = kPtDynamic,
        .flags = 0,
        .offset = offset,
        .virtual_address = 0,
        .physical_address = 0,
        .file_size = file_size,
        .memory_size = file_size,
        .alignment = 8,
        .index = 0,
    };
}

}  // namespace

TEST_CASE("single DT_NULL dynamic table parses", "[loader][dynamic]") {
    std::vector<std::byte> bytes(kEntrySize, std::byte{0});
    auto result =
        astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, bytes.size()));

    REQUIRE(result.has_value());
    REQUIRE(result->entries.size() == 1);
    REQUIRE(result->entries[0].tag == 0);
    REQUIRE(result->entries[0].value == 0);
    REQUIRE(result->entries[0].index == 0);
}

TEST_CASE("dynamic entries preserve order duplicates and unknown tags", "[loader][dynamic]") {
    std::vector<std::byte> bytes(4 * kEntrySize, std::byte{0});
    write_dynamic_entry(bytes, 0, 1, 0x10);
    write_dynamic_entry(bytes, 1, 0x60000001ULL, 0x20);
    write_dynamic_entry(bytes, 2, 1, 0x30);
    write_dynamic_entry(bytes, 3, 0, 0);

    auto result =
        astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, bytes.size()));

    REQUIRE(result.has_value());
    REQUIRE(result->entries.size() == 4);
    REQUIRE(result->entries[0].tag == 1);
    REQUIRE(result->entries[0].value == 0x10);
    REQUIRE(result->entries[1].tag == 0x60000001);
    REQUIRE(result->entries[2].tag == 1);
    REQUIRE(result->entries[2].value == 0x30);
}

TEST_CASE("negative signed dynamic tags preserve their raw bits", "[loader][dynamic]") {
    std::vector<std::byte> bytes(2 * kEntrySize, std::byte{0});
    write_dynamic_entry(bytes, 0, std::numeric_limits<std::uint64_t>::max(), 0x1234);
    write_dynamic_entry(bytes, 1, 0, 0);

    auto result =
        astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, bytes.size()));

    REQUIRE(result.has_value());
    REQUIRE(result->entries[0].tag == -1);
    REQUIRE(result->entries[0].value == 0x1234);
}

TEST_CASE("bytes after first DT_NULL are not interpreted", "[loader][dynamic]") {
    std::vector<std::byte> bytes(3 * kEntrySize, std::byte{0});
    write_dynamic_entry(bytes, 0, 1, 0x10);
    write_dynamic_entry(bytes, 1, 0, 0);
    write_dynamic_entry(bytes, 2, 1, 0xdeadbeef);

    auto result =
        astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, bytes.size()));

    REQUIRE(result.has_value());
    REQUIRE(result->entries.size() == 2);
    REQUIRE(result->entries.back().tag == 0);
}

TEST_CASE("dynamic table parser rejects invalid segment metadata", "[loader][dynamic]") {
    SECTION("not PT_DYNAMIC") {
        std::vector<std::byte> bytes(kEntrySize, std::byte{0});
        auto ph = dynamic_header(0, bytes.size());
        ph.type = 1;
        auto result = astraea::loader::parse_dynamic_table(bytes, ph);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::loader::DynamicErrorCode::not_dynamic_segment);
    }

    SECTION("zero-sized segment") {
        std::vector<std::byte> bytes;
        auto result = astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, 0));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::loader::DynamicErrorCode::missing_terminator);
    }

    SECTION("size not divisible by entry width") {
        std::vector<std::byte> bytes(kEntrySize + 1, std::byte{0});
        auto result =
            astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, bytes.size()));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::loader::DynamicErrorCode::invalid_segment_size);
    }

    SECTION("file range overflow") {
        std::vector<std::byte> bytes(kEntrySize, std::byte{0});
        auto result = astraea::loader::parse_dynamic_table(
            bytes,
            dynamic_header(std::numeric_limits<std::uint64_t>::max() - 8, kEntrySize));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::loader::DynamicErrorCode::integer_overflow);
    }

    SECTION("file range out of bounds") {
        std::vector<std::byte> bytes(kEntrySize, std::byte{0});
        auto result =
            astraea::loader::parse_dynamic_table(bytes, dynamic_header(kEntrySize, kEntrySize));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::loader::DynamicErrorCode::segment_out_of_bounds);
    }
}

TEST_CASE("dynamic table requires DT_NULL inside bounded segment", "[loader][dynamic]") {
    std::vector<std::byte> bytes(2 * kEntrySize, std::byte{0});
    write_dynamic_entry(bytes, 0, 1, 0x10);
    write_dynamic_entry(bytes, 1, 2, 0x20);

    auto result =
        astraea::loader::parse_dynamic_table(bytes, dynamic_header(0, bytes.size()));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == astraea::loader::DynamicErrorCode::missing_terminator);
    REQUIRE(result.error().entry_index == std::size_t{2});
}
