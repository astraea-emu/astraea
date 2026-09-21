#include <astraea/loader/dynamic_metadata.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::SceDynamicTagKind;

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
    std::initializer_list<
        astraea::loader::DynamicEntry> entries) {
    return astraea::loader::DynamicTable{
        .entries =
            std::vector<
                astraea::loader::DynamicEntry>{
                entries},
    };
}

void require_conflict(std::int64_t tag) {
    const auto result =
        astraea::loader::build_sce_dynamic_metadata(
            table({
                entry(tag, 0x1111, 4),
                entry(tag, 0x2222, 9),
            }));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::
            SceDynamicMetadataErrorCode::
                conflicting_singleton_tag);
    REQUIRE(result.error().tag == tag);
    REQUIRE(
        result.error().source_entry_index ==
        std::size_t{9});
    REQUIRE(
        result.error().conflicting_entry_index ==
        std::size_t{4});
}

}  // namespace

TEST_CASE(
    "current SCE dynamic tags are classified without decoding values",
    "[loader][sce-dynamic-metadata]") {
    constexpr std::array<
        std::pair<std::int64_t, SceDynamicTagKind>,
        10>
        expected{{
            {0x61000011,
             SceDynamicTagKind::module_attributes},
            {0x61000017,
             SceDynamicTagKind::
                 export_library_attributes},
            {0x61000019,
             SceDynamicTagKind::
                 import_library_attributes},
            {0x6100003d,
             SceDynamicTagKind::hash_table_size},
            {0x6100003f,
             SceDynamicTagKind::symbol_table_size},
            {0x61000041,
             SceDynamicTagKind::original_filename},
            {0x61000043,
             SceDynamicTagKind::module_information},
            {0x61000045,
             SceDynamicTagKind::needed_module},
            {0x61000047,
             SceDynamicTagKind::export_library},
            {0x61000049,
             SceDynamicTagKind::import_library},
        }};

    astraea::loader::DynamicTable input;
    for (std::size_t index = 0;
         index < expected.size();
         ++index) {
        input.entries.push_back(
            entry(
                expected[index].first,
                0x1000U + index,
                index));
    }

    const auto result =
        astraea::loader::build_sce_dynamic_metadata(
            input);

    REQUIRE(result.has_value());
    REQUIRE(
        result->records.size() ==
        expected.size());

    for (std::size_t index = 0;
         index < expected.size();
         ++index) {
        const auto& record =
            result->records[index];
        REQUIRE(
            record.kind ==
            expected[index].second);
        REQUIRE(
            record.raw_tag ==
            expected[index].first);
        REQUIRE(
            record.raw_value ==
            0x1000U + index);
        REQUIRE(
            record.source_entry_index ==
            index);
    }
}

TEST_CASE(
    "SCE metadata preserves generic and unknown records exactly",
    "[loader][sce-dynamic-metadata]") {
    const auto max =
        std::numeric_limits<std::uint64_t>::max();

    const auto result =
        astraea::loader::build_sce_dynamic_metadata(
            table({
                entry(1, 0x12, 3),
                entry(0x60000002, max, 7),
                entry(0, 0, 11),
            }));

    REQUIRE(result.has_value());
    REQUIRE(result->records.size() == 3);

    REQUIRE(
        result->records[0].kind ==
        SceDynamicTagKind::unknown);
    REQUIRE(result->records[0].raw_tag == 1);
    REQUIRE(result->records[0].raw_value == 0x12);
    REQUIRE(
        result->records[0].source_entry_index ==
        3);

    REQUIRE(
        result->records[1].kind ==
        SceDynamicTagKind::unknown);
    REQUIRE(
        result->records[1].raw_tag ==
        0x60000002);
    REQUIRE(
        result->records[1].raw_value ==
        max);
    REQUIRE(
        result->records[1].source_entry_index ==
        7);

    REQUIRE(
        result->records[2].kind ==
        SceDynamicTagKind::unknown);
    REQUIRE(result->records[2].raw_tag == 0);
    REQUIRE(result->records[2].raw_value == 0);
    REQUIRE(
        result->records[2].source_entry_index ==
        11);
}

TEST_CASE(
    "repeatable SCE records do not invent singleton semantics",
    "[loader][sce-dynamic-metadata]") {
    const auto result =
        astraea::loader::build_sce_dynamic_metadata(
            table({
                entry(0x61000017, 1, 0),
                entry(0x61000017, 2, 1),
                entry(0x61000019, 3, 2),
                entry(0x61000019, 4, 3),
                entry(0x61000045, 5, 4),
                entry(0x61000045, 6, 5),
                entry(0x61000047, 7, 6),
                entry(0x61000047, 8, 7),
                entry(0x61000049, 9, 8),
                entry(0x61000049, 10, 9),
            }));

    REQUIRE(result.has_value());
    REQUIRE(result->records.size() == 10);
}

TEST_CASE(
    "identical singleton SCE records remain preserved",
    "[loader][sce-dynamic-metadata]") {
    const auto result =
        astraea::loader::build_sce_dynamic_metadata(
            table({
                entry(0x61000043, 0x1234, 2),
                entry(0x61000043, 0x1234, 6),
            }));

    REQUIRE(result.has_value());
    REQUIRE(result->records.size() == 2);
    REQUIRE(
        result->records[0].source_entry_index ==
        2);
    REQUIRE(
        result->records[1].source_entry_index ==
        6);
}

TEST_CASE(
    "conflicting file-global SCE singleton records fail deterministically",
    "[loader][sce-dynamic-metadata]") {
    SECTION("module attributes") {
        require_conflict(0x61000011);
    }

    SECTION("hash table size") {
        require_conflict(0x6100003d);
    }

    SECTION("symbol table size") {
        require_conflict(0x6100003f);
    }

    SECTION("original filename") {
        require_conflict(0x61000041);
    }

    SECTION("module information") {
        require_conflict(0x61000043);
    }
}

TEST_CASE(
    "unknown repeated records remain evidence instead of conflicts",
    "[loader][sce-dynamic-metadata]") {
    const auto result =
        astraea::loader::build_sce_dynamic_metadata(
            table({
                entry(0x60000002, 0xaaaa, 1),
                entry(0x60000002, 0xbbbb, 2),
            }));

    REQUIRE(result.has_value());
    REQUIRE(result->records.size() == 2);
    REQUIRE(
        result->records[0].kind ==
        SceDynamicTagKind::unknown);
    REQUIRE(
        result->records[1].kind ==
        SceDynamicTagKind::unknown);
}
