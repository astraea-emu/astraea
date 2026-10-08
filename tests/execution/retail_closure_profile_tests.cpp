#include <astraea/execution/retail_closure_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    const auto widened =
        static_cast<std::uint32_t>(value);
    for (std::size_t i = 0; i < 2; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (widened >> (i * 8U)) & 0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (value >> (i * 8U)) & 0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (value >> (i * 8U)) & 0xffU);
    }
}

std::vector<std::byte> minimal_sce_elf() {
    constexpr std::size_t image_size = 0x200U;
    constexpr std::uint64_t guest_base = 0x00400000U;

    std::vector<std::byte> bytes(
        image_size,
        std::byte{0});

    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    bytes[7] = std::byte{9};
    bytes[8] = std::byte{2};

    write_u16(bytes, 16U, 0xfe10U);
    write_u16(bytes, 18U, 62U);
    write_u32(bytes, 20U, 1U);
    write_u64(bytes, 24U, guest_base + 0x100U);
    write_u64(bytes, 32U, 64U);
    write_u16(bytes, 52U, 64U);
    write_u16(bytes, 54U, 56U);
    write_u16(bytes, 56U, 1U);

    constexpr std::size_t ph = 64U;
    write_u32(bytes, ph, 1U);
    write_u32(bytes, ph + 4U, 0x5U);
    write_u64(bytes, ph + 8U, 0U);
    write_u64(bytes, ph + 16U, guest_base);
    write_u64(bytes, ph + 24U, 0U);
    write_u64(bytes, ph + 32U, image_size);
    write_u64(bytes, ph + 40U, 0x300U);
    write_u64(bytes, ph + 48U, 0x1000U);

    return bytes;
}

}  // namespace

TEST_CASE(
    "raw retail artifact profiling is portable and execution-free",
    "[execution][analysis][retail][closure-profile]") {
    const auto result =
        astraea::execution::profile_retail_artifact(
            minimal_sce_elf());

    REQUIRE(result.has_value());
    REQUIRE(result->program_header_count == 1U);
    REQUIRE(result->load_segment_count == 1U);
    REQUIRE(result->load_memory_bytes == 0x300U);
    REQUIRE(result->executable_load_segment_count == 1U);
    REQUIRE(
        result->executable_load_memory_bytes ==
        0x300U);
    REQUIRE(result->generic_needed_count == 0U);
    REQUIRE(result->sce_needed_module_count == 0U);
    REQUIRE(result->sce_import_library_count == 0U);
    REQUIRE(
        result->sce_unknown_dynamic_record_count ==
        0U);
    REQUIRE_FALSE(result->dynamic_symbol_count.has_value());
    REQUIRE(result->total_relocation_count == 0U);
    REQUIRE_FALSE(result->tls_present);
}

TEST_CASE(
    "raw retail artifact profiling preserves malformed ELF as typed GuestImage failure",
    "[execution][analysis][retail][closure-profile][negative]") {
    const auto result =
        astraea::execution::profile_retail_artifact(
            std::vector<std::byte>{
                std::byte{'n'},
                std::byte{'o'},
                std::byte{'t'},
            });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            RetailArtifactClosureProfileErrorCode::
                guest_image_failure);
    REQUIRE(result.error().guest_image_error.has_value());
    REQUIRE(
        result.error().guest_image_error->code ==
        astraea::loader::GuestImageErrorCode::
            elf_parse_failure);
}
