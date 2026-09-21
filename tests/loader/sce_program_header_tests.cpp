#include <astraea/loader/sce_program_header.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::loader::ProgramHeader make_header(
    std::uint32_t type,
    std::size_t index) {
    return astraea::loader::ProgramHeader{
        .type = type,
        .flags = 0xa5a55a5aU,
        .offset = 0x1111222233334444ULL,
        .virtual_address = 0x5555666677778888ULL,
        .physical_address = 0x9999aaaabbbbccccULL,
        .file_size = 0x123456789abcdef0ULL,
        .memory_size = 0x0fedcba987654321ULL,
        .alignment = 0x2000,
        .index = index,
    };
}

void require_raw_preserved(
    const astraea::loader::ProgramHeader& expected,
    const astraea::loader::ProgramHeader& actual) {
    REQUIRE(actual.type == expected.type);
    REQUIRE(actual.flags == expected.flags);
    REQUIRE(actual.offset == expected.offset);
    REQUIRE(actual.virtual_address == expected.virtual_address);
    REQUIRE(actual.physical_address == expected.physical_address);
    REQUIRE(actual.file_size == expected.file_size);
    REQUIRE(actual.memory_size == expected.memory_size);
    REQUIRE(actual.alignment == expected.alignment);
    REQUIRE(actual.index == expected.index);
}

}  // namespace

TEST_CASE(
    "evidence-backed SCE program-header values are classified exactly",
    "[loader][sce-program-header]") {
    using astraea::loader::SceProgramHeaderKind;

    constexpr std::array<
        std::pair<std::uint32_t, SceProgramHeaderKind>,
        3>
        expected{{
            {0x61000000U, SceProgramHeaderKind::dynlib_data},
            {0x61000001U, SceProgramHeaderKind::process_parameter},
            {0x61000010U, SceProgramHeaderKind::relro},
        }};

    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto raw = make_header(
            expected[index].first,
            index + 7);
        const auto record =
            astraea::loader::classify_sce_program_header(raw);

        REQUIRE(record.kind == expected[index].second);
        require_raw_preserved(raw, record.raw);
    }
}

TEST_CASE(
    "generic and unsupported program headers remain typed unknown evidence",
    "[loader][sce-program-header]") {
    using astraea::loader::SceProgramHeaderKind;

    constexpr std::array<std::uint32_t, 6> unknown_types{
        0U,
        1U,
        2U,
        0x60000001U,
        0x61000002U,
        std::numeric_limits<std::uint32_t>::max(),
    };

    for (std::size_t index = 0; index < unknown_types.size(); ++index) {
        const auto raw = make_header(
            unknown_types[index],
            index + 100);
        const auto record =
            astraea::loader::classify_sce_program_header(raw);

        REQUIRE(record.kind == SceProgramHeaderKind::unknown);
        require_raw_preserved(raw, record.raw);
    }
}

TEST_CASE(
    "SCE program-header classification does not infer runtime semantics",
    "[loader][sce-program-header]") {
    auto raw = make_header(0x61000010U, 31);
    raw.flags = std::numeric_limits<std::uint32_t>::max();
    raw.offset = std::numeric_limits<std::uint64_t>::max();
    raw.virtual_address = std::numeric_limits<std::uint64_t>::max();
    raw.physical_address = std::numeric_limits<std::uint64_t>::max();
    raw.file_size = std::numeric_limits<std::uint64_t>::max();
    raw.memory_size = 0;
    raw.alignment = 3;

    const auto record =
        astraea::loader::classify_sce_program_header(raw);

    REQUIRE(
        record.kind ==
        astraea::loader::SceProgramHeaderKind::relro);
    require_raw_preserved(raw, record.raw);
}
