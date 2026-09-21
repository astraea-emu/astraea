#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/initialized_image_view.hpp>
#include <astraea/memory/mapping.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint64_t kGuestBase = 0x1000;
constexpr std::uint64_t kStringTableBase = 0x1100;
constexpr std::size_t kImageSize = 0x200;
constexpr std::size_t kSymbolSize = 24;
constexpr std::size_t kStringFileOffset = 0x100;
constexpr std::uint64_t kStringTableSize = 0x80;

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    for (std::size_t index = 0;
         index < 2;
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (index * 8U)) &
                0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0;
         index < 4;
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (index * 8U)) &
                0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t index = 0;
         index < 8;
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8U)) &
                0xffU);
    }
}

void write_symbol(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t name_offset) {
    write_u32(bytes, offset, name_offset);
    bytes[offset + 4U] = std::byte{0x12};
    bytes[offset + 5U] = std::byte{0x00};
    write_u16(bytes, offset + 6U, 0);
    write_u64(bytes, offset + 8U, 0);
    write_u64(bytes, offset + 16U, 0);
}

void write_string(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    const std::string& value) {
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    value[index]));
    }
    bytes[offset + value.size()] =
        std::byte{0};
}

astraea::memory::GuestRange range(
    std::uint64_t base,
    std::uint64_t size) {
    auto result =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{base},
            astraea::memory::GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

astraea::memory::MappingIntent image_mapping() {
    auto permissions =
        astraea::memory::GuestPermissions::
            checked_from_bits(0x4U);
    REQUIRE(permissions.has_value());

    return astraea::memory::MappingIntent{
        .range =
            range(
                kGuestBase,
                static_cast<std::uint64_t>(
                    kImageSize)),
        .permissions = permissions.value(),
        .backing =
            astraea::memory::MappingBacking{
                .kind =
                    astraea::memory::
                        MappingBackingKind::file,
                .file_offset = 0,
                .byte_count =
                    astraea::memory::GuestSize{
                        static_cast<std::uint64_t>(
                            kImageSize)},
            },
        .source_index = 0,
    };
}

astraea::loader::DynamicSymbolTableDescriptor
symbol_descriptor() {
    return astraea::loader::
        DynamicSymbolTableDescriptor{
            .range =
                range(
                    kGuestBase,
                    2U * kSymbolSize),
            .entry_size =
                astraea::memory::GuestSize{
                    kSymbolSize},
            .symbol_count = 2,
            .count_from_symtabsz = true,
            .count_from_sysv_hash = false,
            .symtab_source_entry_index = 3,
            .syment_source_entry_index = 4,
            .symtabsz_source_entry_index =
                std::size_t{5},
            .hash_source_entry_index =
                std::nullopt,
        };
}

astraea::loader::DynamicStringTableDescriptor
string_descriptor() {
    return astraea::loader::
        DynamicStringTableDescriptor{
            .range =
                range(
                    kStringTableBase,
                    kStringTableSize),
        };
}

astraea::memory::InitializedImageView make_view(
    const std::vector<std::byte>& bytes) {
    const auto mapping = image_mapping();
    const std::array mappings{mapping};
    auto view =
        astraea::memory::
            InitializedImageView::create(
                bytes,
                mappings);
    REQUIRE(view.has_value());
    return view.value();
}

std::vector<std::byte> image_with_name(
    const std::string& name,
    std::uint32_t name_offset = 1) {
    std::vector<std::byte> bytes(
        kImageSize,
        std::byte{0});

    write_symbol(
        bytes,
        kSymbolSize,
        name_offset);

    if (name_offset < kStringTableSize) {
        write_string(
            bytes,
            kStringFileOffset +
                static_cast<std::size_t>(
                    name_offset),
            name);
    }

    return bytes;
}

}  // namespace

TEST_CASE(
    "SCE dynamic symbol materialization preserves generic symbol and exact identity",
    "[loader][sce-dynamic-symbol]") {
    const std::string raw =
        "ABCDEFGHIJK#library-a#module-a";
    const auto bytes = image_with_name(raw);
    const auto view = make_view(bytes);

    const auto result =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                symbol_descriptor(),
                string_descriptor(),
                1,
                view);

    REQUIRE(result.has_value());
    REQUIRE(result->symbol.index == 1);
    REQUIRE(result->symbol.name_offset == 1);
    REQUIRE(result->name.raw == raw);
    REQUIRE(result->name.identity.has_value());
    REQUIRE(
        result->name.identity->nid ==
        "ABCDEFGHIJK");
    REQUIRE(
        result->name.identity->library_id ==
        "library-a");
    REQUIRE(
        result->name.identity->module_id ==
        "module-a");
}

TEST_CASE(
    "plain dynamic symbol spelling remains opaque",
    "[loader][sce-dynamic-symbol]") {
    const std::string raw = "plain_symbol";
    const auto bytes = image_with_name(raw);
    const auto view = make_view(bytes);

    const auto result =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                symbol_descriptor(),
                string_descriptor(),
                1,
                view);

    REQUIRE(result.has_value());
    REQUIRE(result->name.raw == raw);
    REQUIRE_FALSE(
        result->name.identity.has_value());
}

TEST_CASE(
    "malformed SCE candidate preserves typed identity failure and symbol index",
    "[loader][sce-dynamic-symbol]") {
    const auto bytes =
        image_with_name(
            "ABCDEFGHIJ#library-a#module-a");
    const auto view = make_view(bytes);

    const auto result =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                symbol_descriptor(),
                string_descriptor(),
                1,
                view);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::
            SceDynamicSymbolErrorCode::
                sce_identity_failure);
    REQUIRE(result.error().symbol_index == 1);
    REQUIRE(
        result.error().identity_error.has_value());
    REQUIRE(
        result.error().identity_error->code ==
        astraea::loader::
            SceSymbolIdentityErrorCode::
                invalid_nid_length);
}

TEST_CASE(
    "dynamic symbol failure remains typed through SCE materialization",
    "[loader][sce-dynamic-symbol]") {
    const auto bytes =
        image_with_name("plain_symbol");
    const auto view = make_view(bytes);

    const auto result =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                symbol_descriptor(),
                string_descriptor(),
                2,
                view);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::
            SceDynamicSymbolErrorCode::
                dynamic_symbol_failure);
    REQUIRE(result.error().symbol_index == 2);
    REQUIRE(
        result.error().symbol_error.has_value());
    REQUIRE(
        result.error().symbol_error->code ==
        astraea::loader::
            DynamicSymbolErrorCode::
                symbol_index_out_of_bounds);
}

TEST_CASE(
    "dynamic string failure remains typed through SCE materialization",
    "[loader][sce-dynamic-symbol]") {
    const auto bytes =
        image_with_name(
            "",
            static_cast<std::uint32_t>(
                kStringTableSize));
    const auto view = make_view(bytes);

    const auto result =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                symbol_descriptor(),
                string_descriptor(),
                1,
                view);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::
            SceDynamicSymbolErrorCode::
                dynamic_string_failure);
    REQUIRE(result.error().symbol_index == 1);
    REQUIRE(
        result.error().string_error.has_value());
    REQUIRE(
        result.error().string_error->code ==
        astraea::loader::
            DynamicStringErrorCode::
                offset_out_of_bounds);
}
