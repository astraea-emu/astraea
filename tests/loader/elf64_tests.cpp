#include <astraea/loader/elf64.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kProgramHeaderOffset = 64;

constexpr std::uint32_t kPtNull = 0;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtOsSpecific = 0x60000001U;

void write_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 2; ++i) {
        bytes[offset + i] = static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

void write_u64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

std::vector<std::byte> make_elf(std::uint16_t program_header_count = 0) {
    const auto size =
        kProgramHeaderOffset + static_cast<std::size_t>(program_header_count) * kProgramHeaderSize;
    std::vector<std::byte> bytes(std::max(size, kElfHeaderSize), std::byte{0});

    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};

    write_u16(bytes, 16, 2);
    write_u16(bytes, 18, 62);
    write_u32(bytes, 20, 1);
    write_u64(bytes, 24, 0x400000);
    write_u64(bytes, 32, program_header_count == 0 ? 0 : kProgramHeaderOffset);
    write_u16(bytes, 52, kElfHeaderSize);
    write_u16(bytes, 54, program_header_count == 0 ? 0 : kProgramHeaderSize);
    write_u16(bytes, 56, program_header_count);

    return bytes;
}

void write_program_header(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::uint32_t type,
    std::uint32_t flags,
    std::uint64_t offset,
    std::uint64_t virtual_address,
    std::uint64_t file_size,
    std::uint64_t memory_size,
    std::uint64_t alignment) {
    const auto base = kProgramHeaderOffset + index * kProgramHeaderSize;
    write_u32(bytes, base, type);
    write_u32(bytes, base + 4, flags);
    write_u64(bytes, base + 8, offset);
    write_u64(bytes, base + 16, virtual_address);
    write_u64(bytes, base + 24, 0);
    write_u64(bytes, base + 32, file_size);
    write_u64(bytes, base + 40, memory_size);
    write_u64(bytes, base + 48, alignment);
}

void extend_to(std::vector<std::byte>& bytes, std::size_t size) {
    if (bytes.size() < size) {
        bytes.resize(size, std::byte{0});
    }
}

void require_error(
    const std::vector<std::byte>& bytes,
    astraea::loader::ElfErrorCode expected_code) {
    const auto result = astraea::loader::parse_elf64(bytes);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == expected_code);
}

}  // namespace

TEST_CASE("minimal ELF64 with no program headers parses", "[loader][elf64]") {
    auto bytes = make_elf();
    bytes[7] = std::byte{9};
    bytes[8] = std::byte{4};
    write_u32(bytes, 48, 0x12345678U);

    const auto result = astraea::loader::parse_elf64(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->program_headers.empty());
    REQUIRE(result->header.entry == 0x400000);
    REQUIRE(result->header.os_abi == 9);
    REQUIRE(result->header.abi_version == 4);
    REQUIRE(result->header.flags == 0x12345678U);
}

TEST_CASE("one RX PT_LOAD parses and preserves metadata", "[loader][elf64]") {
    auto bytes = make_elf(1);
    extend_to(bytes, 0x200);
    write_program_header(bytes, 0, kPtLoad, 0x5, 0x100, 0x40100, 0x20, 0x30, 0x100);

    const auto result = astraea::loader::parse_elf64(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->program_headers.size() == 1);
    const auto& ph = result->program_headers.front();
    REQUIRE(ph.type == kPtLoad);
    REQUIRE(ph.flags == 0x5);
    REQUIRE(ph.offset == 0x100);
    REQUIRE(ph.virtual_address == 0x40100);
    REQUIRE(ph.file_size == 0x20);
    REQUIRE(ph.memory_size == 0x30);
    REQUIRE(ph.alignment == 0x100);
    REQUIRE(ph.index == 0);
}

TEST_CASE("multiple PT_LOAD entries may include zero-fill and zero file size", "[loader][elf64]") {
    auto bytes = make_elf(2);
    extend_to(bytes, 0x300);
    write_program_header(bytes, 0, kPtLoad, 0x5, 0x100, 0x40100, 0x20, 0x40, 0x100);
    write_program_header(bytes, 1, kPtLoad, 0x6, 0x200, 0x50200, 0, 0x80, 1);

    const auto result = astraea::loader::parse_elf64(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->program_headers.size() == 2);
    REQUIRE(result->program_headers[1].file_size == 0);
    REQUIRE(result->program_headers[1].memory_size == 0x80);
}

TEST_CASE("unknown OS-specific headers are preserved without invented semantics", "[loader][elf64]") {
    auto bytes = make_elf(1);
    write_program_header(
        bytes,
        0,
        kPtOsSpecific,
        0xdeadbeefU,
        std::numeric_limits<std::uint64_t>::max(),
        0xf000000000000000ULL,
        std::numeric_limits<std::uint64_t>::max(),
        1,
        3);

    const auto result = astraea::loader::parse_elf64(bytes);

    REQUIRE(result.has_value());
    REQUIRE(result->program_headers.front().type == kPtOsSpecific);
    REQUIRE(
        result->program_headers.front().offset == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("PT_NULL ignores undefined non-type fields", "[loader][elf64]") {
    auto bytes = make_elf(1);
    write_program_header(
        bytes,
        0,
        kPtNull,
        0xffffffffU,
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        0,
        3);

    REQUIRE(astraea::loader::parse_elf64(bytes).has_value());
}

TEST_CASE("truncation before complete ELF header is rejected", "[loader][elf64]") {
    const auto complete = make_elf();

    for (std::size_t size = 0; size < kElfHeaderSize; ++size) {
        auto truncated = complete;
        truncated.resize(size);
        require_error(truncated, astraea::loader::ElfErrorCode::file_too_small);
    }
}

TEST_CASE("bad magic bytes are rejected deterministically", "[loader][elf64]") {
    for (std::size_t index = 0; index < 4; ++index) {
        auto bytes = make_elf();
        bytes[index] = std::byte{0};
        const auto result = astraea::loader::parse_elf64(bytes);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::loader::ElfErrorCode::bad_magic);
        REQUIRE(result.error().file_offset == index);
    }
}

TEST_CASE("unsupported ELF identification values are rejected", "[loader][elf64]") {
    SECTION("ELF32") {
        auto bytes = make_elf();
        bytes[4] = std::byte{1};
        require_error(bytes, astraea::loader::ElfErrorCode::unsupported_class);
    }
    SECTION("big endian") {
        auto bytes = make_elf();
        bytes[5] = std::byte{2};
        require_error(bytes, astraea::loader::ElfErrorCode::unsupported_endianness);
    }
    SECTION("ident version") {
        auto bytes = make_elf();
        bytes[6] = std::byte{0};
        require_error(bytes, astraea::loader::ElfErrorCode::unsupported_ident_version);
    }
}

TEST_CASE("unsupported ELF header values are rejected", "[loader][elf64]") {
    SECTION("ELF version") {
        auto bytes = make_elf();
        write_u32(bytes, 20, 0);
        require_error(bytes, astraea::loader::ElfErrorCode::unsupported_elf_version);
    }
    SECTION("machine") {
        auto bytes = make_elf();
        write_u16(bytes, 18, 40);
        require_error(bytes, astraea::loader::ElfErrorCode::unsupported_machine);
    }
    SECTION("file type") {
        auto bytes = make_elf();
        write_u16(bytes, 16, 1);
        require_error(bytes, astraea::loader::ElfErrorCode::unsupported_file_type);
    }
    SECTION("header size") {
        auto bytes = make_elf();
        write_u16(bytes, 52, 63);
        require_error(bytes, astraea::loader::ElfErrorCode::invalid_elf_header_size);
    }
}

TEST_CASE("program-header table validation rejects unsupported or invalid layout", "[loader][elf64]") {
    SECTION("extended program-header count") {
        auto bytes = make_elf();
        write_u16(bytes, 56, 0xffff);
        require_error(
            bytes,
            astraea::loader::ElfErrorCode::unsupported_extended_program_header_count);
    }
    SECTION("wrong entry size") {
        auto bytes = make_elf(1);
        write_u16(bytes, 54, 55);
        require_error(
            bytes,
            astraea::loader::ElfErrorCode::invalid_program_header_entry_size);
    }
    SECTION("table offset addition overflow") {
        auto bytes = make_elf(1);
        write_u64(bytes, 32, std::numeric_limits<std::uint64_t>::max() - 10);
        require_error(bytes, astraea::loader::ElfErrorCode::integer_overflow);
    }
    SECTION("table out of bounds") {
        auto bytes = make_elf(1);
        bytes.resize(kElfHeaderSize);
        require_error(
            bytes,
            astraea::loader::ElfErrorCode::program_header_table_out_of_bounds);
    }
}

TEST_CASE("PT_LOAD file and memory ranges are validated", "[loader][elf64]") {
    SECTION("file size exceeds memory size") {
        auto bytes = make_elf(1);
        write_program_header(bytes, 0, kPtLoad, 0x5, 0, 0, 2, 1, 0);
        require_error(
            bytes,
            astraea::loader::ElfErrorCode::load_file_size_exceeds_memory_size);
    }
    SECTION("file range addition overflow") {
        auto bytes = make_elf(1);
        write_program_header(
            bytes,
            0,
            kPtLoad,
            0x5,
            std::numeric_limits<std::uint64_t>::max(),
            0,
            2,
            2,
            0);
        require_error(bytes, astraea::loader::ElfErrorCode::integer_overflow);
    }
    SECTION("file range out of bounds") {
        auto bytes = make_elf(1);
        write_program_header(bytes, 0, kPtLoad, 0x5, 0x1000, 0, 1, 1, 0);
        require_error(
            bytes,
            astraea::loader::ElfErrorCode::segment_file_range_out_of_bounds);
    }
    SECTION("virtual range overflow") {
        auto bytes = make_elf(1);
        write_program_header(
            bytes,
            0,
            kPtLoad,
            0x5,
            0,
            std::numeric_limits<std::uint64_t>::max(),
            0,
            2,
            0);
        require_error(
            bytes,
            astraea::loader::ElfErrorCode::load_virtual_range_overflow);
    }
    SECTION("last guest byte may be UINT64_MAX") {
        auto bytes = make_elf(1);
        write_program_header(
            bytes,
            0,
            kPtLoad,
            0x5,
            0,
            std::numeric_limits<std::uint64_t>::max(),
            0,
            1,
            0);
        REQUIRE(astraea::loader::parse_elf64(bytes).has_value());
    }
}

TEST_CASE("PT_LOAD alignment is validated", "[loader][elf64]") {
    SECTION("alignment zero") {
        auto bytes = make_elf(1);
        write_program_header(bytes, 0, kPtLoad, 0x5, 0, 0x1000, 0, 0, 0);
        REQUIRE(astraea::loader::parse_elf64(bytes).has_value());
    }
    SECTION("alignment one") {
        auto bytes = make_elf(1);
        write_program_header(bytes, 0, kPtLoad, 0x5, 0, 0x1000, 0, 0, 1);
        REQUIRE(astraea::loader::parse_elf64(bytes).has_value());
    }
    SECTION("non-power-of-two") {
        auto bytes = make_elf(1);
        write_program_header(bytes, 0, kPtLoad, 0x5, 0, 0x1000, 0, 0, 3);
        require_error(bytes, astraea::loader::ElfErrorCode::invalid_load_alignment);
    }
    SECTION("offset and virtual address incongruent") {
        auto bytes = make_elf(1);
        write_program_header(bytes, 0, kPtLoad, 0x5, 0x100, 0x200, 0, 0, 0x1000);
        require_error(bytes, astraea::loader::ElfErrorCode::invalid_load_alignment);
    }
}

TEST_CASE("PT_LOAD entries must be ordered by virtual address", "[loader][elf64]") {
    auto bytes = make_elf(2);
    write_program_header(bytes, 0, kPtLoad, 0x5, 0, 0x2000, 0, 0, 0);
    write_program_header(bytes, 1, kPtLoad, 0x5, 0, 0x1000, 0, 0, 0);

    const auto result = astraea::loader::parse_elf64(bytes);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == astraea::loader::ElfErrorCode::load_segments_out_of_order);
    REQUIRE(result.error().program_header_index == 1);
}


TEST_CASE(
    "SCE ELF file types require the explicit PS5/SCE parse profile",
    "[loader][elf64][sce]") {
    constexpr std::array<std::uint16_t, 2> supported{
        0xfe10,
        0xfe18,
    };

    for (const auto type : supported) {
        DYNAMIC_SECTION("type 0x" << std::hex << type) {
            auto bytes = make_elf();
            write_u16(bytes, 16, type);

            const auto generic =
                astraea::loader::parse_elf64(bytes);
            REQUIRE_FALSE(generic.has_value());
            REQUIRE(
                generic.error().code ==
                astraea::loader::ElfErrorCode::
                    unsupported_file_type);

            const auto sce =
                astraea::loader::parse_elf64(
                    bytes,
                    astraea::loader::
                        ElfParseProfile::ps5_sce);
            REQUIRE(sce.has_value());
            REQUIRE(sce->header.type == type);
        }
    }
}

TEST_CASE(
    "PS5/SCE ELF profile rejects undocumented neighboring file type",
    "[loader][elf64][sce]") {
    auto bytes = make_elf();
    write_u16(bytes, 16, 0xfe11);

    const auto result =
        astraea::loader::parse_elf64(
            bytes,
            astraea::loader::
                ElfParseProfile::ps5_sce);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::ElfErrorCode::
            unsupported_file_type);
}

TEST_CASE(
    "PS5/SCE ELF profile does not silently accept generic ELF file types",
    "[loader][elf64][sce]") {
    auto bytes = make_elf();

    const auto result =
        astraea::loader::parse_elf64(
            bytes,
            astraea::loader::
                ElfParseProfile::ps5_sce);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::ElfErrorCode::
            unsupported_file_type);
}
