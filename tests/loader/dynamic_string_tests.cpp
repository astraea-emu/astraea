#include <astraea/loader/dynamic_string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <initializer_list>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::DynamicStringErrorCode;
using astraea::loader::DynamicStringRef;
using astraea::loader::DynamicStringTableDescriptor;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::InitializedImageView;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

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

MappingIntent zero_intent(
    std::uint64_t guest_base,
    std::uint64_t size,
    std::size_t source_index) {
    return MappingIntent{
        .range = range(guest_base, size),
        .permissions = read_only(),
        .backing =
            MappingBacking{
                .kind = MappingBackingKind::zero_fill,
                .file_offset = 0,
                .byte_count = GuestSize{size},
            },
        .source_index = source_index,
    };
}

std::vector<std::byte> bytes(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

DynamicStringTableDescriptor table(std::uint64_t base, std::uint64_t size) {
    return DynamicStringTableDescriptor{.range = range(base, size)};
}

DynamicStringRef reference(std::uint64_t offset, std::size_t source_index = 7) {
    return DynamicStringRef{
        .offset = offset,
        .source_entry_index = source_index,
    };
}

}  // namespace

TEST_CASE("dynamic string resolves from first valid offset", "[loader][dynamic-string]") {
    auto image = bytes({'a', 'b', 'c', 0});
    std::array intents{file_intent(0x1000, 4, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x1000, 4), reference(0), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "abc");
}

TEST_CASE("dynamic string supports empty and last-offset strings", "[loader][dynamic-string]") {
    auto image = bytes({0, 'x', 0});
    std::array intents{file_intent(0x2000, 3, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto empty =
        astraea::loader::resolve_dynamic_string(table(0x2000, 3), reference(0), view.value());
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());

    auto last =
        astraea::loader::resolve_dynamic_string(table(0x2000, 3), reference(2), view.value());
    REQUIRE(last.has_value());
    REQUIRE(last->empty());
}

TEST_CASE("dynamic string offset must be strictly within table", "[loader][dynamic-string]") {
    auto image = bytes({0});
    std::array intents{file_intent(0x3000, 1, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x3000, 1), reference(1, 12), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicStringErrorCode::offset_out_of_bounds);
    REQUIRE(result.error().source_entry_index == 12);
}

TEST_CASE("zero-sized dynamic string table has no valid offsets", "[loader][dynamic-string]") {
    auto image = bytes({});
    std::array<MappingIntent, 0> intents{};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x4000, 0), reference(0), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicStringErrorCode::offset_out_of_bounds);
}

TEST_CASE("dynamic string requires terminator within table", "[loader][dynamic-string]") {
    auto image = bytes({'a', 'b', 'c'});
    std::array intents{file_intent(0x5000, 3, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x5000, 3), reference(0), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicStringErrorCode::unterminated);
}

TEST_CASE("NUL at final dynamic string table byte is valid", "[loader][dynamic-string]") {
    auto image = bytes({'a', 'b', 0});
    std::array intents{file_intent(0x6000, 3, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x6000, 3), reference(0), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "ab");
}

TEST_CASE("dynamic string crosses adjacent file-backed mappings", "[loader][dynamic-string]") {
    auto image = bytes({'a', 'b', 'c', 0});
    std::array intents{
        file_intent(0x7000, 2, 0, 0),
        file_intent(0x7002, 2, 2, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x7000, 4), reference(0), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "abc");
}

TEST_CASE("zero-fill may provide dynamic string terminator", "[loader][dynamic-string]") {
    auto image = bytes({'a'});
    std::array intents{
        file_intent(0x8000, 1, 0, 0),
        zero_intent(0x8001, 1, 1),
    };
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x8000, 2), reference(0), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "a");
}

TEST_CASE("dynamic string propagates initialized-image read failure", "[loader][dynamic-string]") {
    auto image = bytes({'a'});
    std::array intents{file_intent(0x9000, 1, 0, 4)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(0x9000, 2), reference(0, 17), view.value());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == DynamicStringErrorCode::image_read_failure);
    REQUIRE(result.error().source_entry_index == 17);
    REQUIRE(result.error().guest_address == GuestAddress{0x9001});
    REQUIRE(result.error().image_error.has_value());
    REQUIRE(
        result.error().image_error->code ==
        astraea::memory::InitializedImageErrorCode::unmapped_guest_address);
}

TEST_CASE("dynamic string resolves across final uint64 guest address", "[loader][dynamic-string]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    auto image = bytes({'z', 0});
    std::array intents{file_intent(max - 1, 2, 0, 0)};
    auto view = InitializedImageView::create(image, intents);
    REQUIRE(view.has_value());

    auto result =
        astraea::loader::resolve_dynamic_string(table(max - 1, 2), reference(0), view.value());

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "z");

    auto final_empty =
        astraea::loader::resolve_dynamic_string(table(max - 1, 2), reference(1), view.value());
    REQUIRE(final_empty.has_value());
    REQUIRE(final_empty->empty());
}
