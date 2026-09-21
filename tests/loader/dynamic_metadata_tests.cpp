#include <astraea/loader/dynamic_metadata.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::loader::DynamicEntry entry(
    std::int64_t tag,
    std::uint64_t value,
    std::size_t index) {
    return astraea::loader::DynamicEntry{
        .tag = tag,
        .value = value,
        .index = index,
    };
}

astraea::loader::DynamicTable table(
    std::initializer_list<astraea::loader::DynamicEntry> entries) {
    return astraea::loader::DynamicTable{
        .entries = std::vector<astraea::loader::DynamicEntry>{entries},
    };
}

}  // namespace

TEST_CASE("dynamic string metadata may be absent", "[loader][dynamic-metadata]") {
    auto result = astraea::loader::build_dynamic_string_metadata(
        table({entry(0x60000001, 0x1234, 0), entry(0, 0, 1)}));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->string_table.has_value());
    REQUIRE(result->needed.empty());
    REQUIRE_FALSE(result->soname.has_value());
}

TEST_CASE("STRTAB and STRSZ form a validated guest range", "[loader][dynamic-metadata]") {
    auto result = astraea::loader::build_dynamic_string_metadata(
        table({entry(5, 0x4000, 0), entry(10, 0x100, 1), entry(0, 0, 2)}));

    REQUIRE(result.has_value());
    REQUIRE(result->string_table.has_value());
    REQUIRE(result->string_table->range.base().value() == 0x4000);
    REQUIRE(result->string_table->range.size().value() == 0x100);
}

TEST_CASE("identical singleton dynamic tags are accepted", "[loader][dynamic-metadata]") {
    auto result = astraea::loader::build_dynamic_string_metadata(
        table({
            entry(5, 0x4000, 0),
            entry(5, 0x4000, 1),
            entry(10, 0x20, 2),
            entry(10, 0x20, 3),
            entry(14, 4, 4),
            entry(14, 4, 5),
            entry(0, 0, 6),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->soname.has_value());
    REQUIRE(result->soname->offset == 4);
    REQUIRE(result->soname->source_entry_index == 4);
}

TEST_CASE("conflicting singleton tags are rejected deterministically", "[loader][dynamic-metadata]") {
    SECTION("STRTAB") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({entry(5, 0x4000, 0), entry(5, 0x5000, 1), entry(10, 0x20, 2)}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::conflicting_dynamic_tag);
        REQUIRE(result.error().tag == 5);
        REQUIRE(result.error().source_entry_index == std::size_t{1});
        REQUIRE(result.error().conflicting_entry_index == std::size_t{0});
    }

    SECTION("STRSZ") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({entry(5, 0x4000, 0), entry(10, 0x20, 1), entry(10, 0x30, 2)}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::conflicting_dynamic_tag);
        REQUIRE(result.error().tag == 10);
    }

    SECTION("SONAME") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({
                entry(5, 0x4000, 0),
                entry(10, 0x20, 1),
                entry(14, 2, 2),
                entry(14, 3, 3),
            }));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::conflicting_dynamic_tag);
        REQUIRE(result.error().tag == 14);
    }
}

TEST_CASE("DT_NEEDED references remain ordered", "[loader][dynamic-metadata]") {
    auto result = astraea::loader::build_dynamic_string_metadata(
        table({
            entry(5, 0x8000, 0),
            entry(10, 0x100, 1),
            entry(1, 7, 2),
            entry(1, 19, 3),
            entry(1, 42, 4),
            entry(0, 0, 5),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->needed.size() == 3);
    REQUIRE(result->needed[0].offset == 7);
    REQUIRE(result->needed[0].source_entry_index == 2);
    REQUIRE(result->needed[1].offset == 19);
    REQUIRE(result->needed[2].offset == 42);
}

TEST_CASE("SONAME is retained as a bounded-table offset", "[loader][dynamic-metadata]") {
    auto result = astraea::loader::build_dynamic_string_metadata(
        table({
            entry(5, 0x8000, 0),
            entry(10, 0x100, 1),
            entry(14, 31, 2),
            entry(0, 0, 3),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->soname.has_value());
    REQUIRE(result->soname->offset == 31);
    REQUIRE(result->soname->source_entry_index == 2);
}

TEST_CASE("dynamic string table companion tags are required together", "[loader][dynamic-metadata]") {
    SECTION("STRTAB without STRSZ") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({entry(5, 0x4000, 0), entry(0, 0, 1)}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::missing_required_companion_tag);
        REQUIRE(result.error().tag == 10);
    }

    SECTION("STRSZ without STRTAB") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({entry(10, 0x20, 0), entry(0, 0, 1)}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::missing_required_companion_tag);
        REQUIRE(result.error().tag == 5);
    }
}

TEST_CASE("string references require a string table", "[loader][dynamic-metadata]") {
    SECTION("NEEDED") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({entry(1, 2, 0), entry(0, 0, 1)}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::missing_string_table);
        REQUIRE(result.error().tag == 1);
    }

    SECTION("SONAME") {
        auto result = astraea::loader::build_dynamic_string_metadata(
            table({entry(14, 2, 0), entry(0, 0, 1)}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::loader::DynamicMetadataErrorCode::missing_string_table);
        REQUIRE(result.error().tag == 14);
    }
}

TEST_CASE("dynamic string table supports the final uint64 byte", "[loader][dynamic-metadata]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto result = astraea::loader::build_dynamic_string_metadata(
        table({entry(5, max, 0), entry(10, 1, 1), entry(0, 0, 2)}));

    REQUIRE(result.has_value());
    REQUIRE(result->string_table.has_value());
    REQUIRE(result->string_table->range.base().value() == max);
    REQUIRE(result->string_table->range.size().value() == 1);
}

TEST_CASE("dynamic string table range overflow is rejected", "[loader][dynamic-metadata]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();

    auto result = astraea::loader::build_dynamic_string_metadata(
        table({entry(5, max, 0), entry(10, 2, 1), entry(0, 0, 2)}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::DynamicMetadataErrorCode::guest_table_range_overflow);
}

TEST_CASE("unknown dynamic tags do not affect validated string metadata", "[loader][dynamic-metadata]") {
    auto result = astraea::loader::build_dynamic_string_metadata(
        table({
            entry(0x60000002, 0xabcdef, 0),
            entry(5, 0x9000, 1),
            entry(10, 0x20, 2),
            entry(0x70000003, 0x123456, 3),
            entry(0, 0, 4),
        }));

    REQUIRE(result.has_value());
    REQUIRE(result->string_table.has_value());
    REQUIRE(result->needed.empty());
    REQUIRE_FALSE(result->soname.has_value());
}
