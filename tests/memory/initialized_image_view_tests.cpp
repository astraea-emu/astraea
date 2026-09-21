#include <astraea/memory/initialized_image_view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <initializer_list>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::memory::GuestAddress;
using astraea::memory::GuestPermission;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::InitializedImageErrorCode;
using astraea::memory::InitializedImageView;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

GuestRange range(std::uint64_t base, std::uint64_t size) {
    auto result = GuestRange::create(GuestAddress{base}, GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

GuestPermissions permissions(std::uint8_t bits = 0x4U) {
    auto result = GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
}

MappingIntent file_intent(
    std::uint64_t guest_base,
    std::uint64_t size,
    std::uint64_t file_offset,
    std::size_t source_index,
    std::uint8_t permission_bits = 0x4U) {
    return MappingIntent{
        .range = range(guest_base, size),
        .permissions = permissions(permission_bits),
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
    std::size_t source_index,
    std::uint8_t permission_bits = 0x4U) {
    return MappingIntent{
        .range = range(guest_base, size),
        .permissions = permissions(permission_bits),
        .backing =
            MappingBacking{
                .kind = MappingBackingKind::zero_fill,
                .file_offset = 0,
                .byte_count = GuestSize{size},
            },
        .source_index = source_index,
    };
}

std::vector<std::byte> image(std::initializer_list<std::uint8_t> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (const auto value : values) {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

}  // namespace

TEST_CASE("initialized image view validates file-backed ranges", "[memory][image-view]") {
    auto bytes = image({0x10, 0x20, 0x30, 0x40});

    SECTION("valid range ending exactly at file end") {
        std::array intents{file_intent(0x1000, 2, 2, 0)};
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());
        REQUIRE(view->mapping_count() == 1);
    }

    SECTION("file range beyond image") {
        std::array intents{file_intent(0x1000, 2, 3, 0)};
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE_FALSE(view.has_value());
        REQUIRE(
            view.error().code ==
            InitializedImageErrorCode::invalid_file_backing_range);
    }

    SECTION("file offset addition overflow") {
        std::array intents{
            file_intent(
                0x1000,
                2,
                std::numeric_limits<std::uint64_t>::max(),
                0)};
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE_FALSE(view.has_value());
        REQUIRE(
            view.error().code ==
            InitializedImageErrorCode::invalid_file_backing_range);
    }
}

TEST_CASE("initialized image view validates backing sizes and kinds", "[memory][image-view]") {
    auto bytes = image({0x10, 0x20});

    SECTION("file backing size mismatch") {
        auto intent = file_intent(0x1000, 1, 0, 0);
        intent.backing.byte_count = GuestSize{2};
        std::array intents{intent};

        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE_FALSE(view.has_value());
        REQUIRE(
            view.error().code ==
            InitializedImageErrorCode::backing_size_mismatch);
    }

    SECTION("zero-fill backing size mismatch") {
        auto intent = zero_intent(0x1000, 1, 0);
        intent.backing.byte_count = GuestSize{2};
        std::array intents{intent};

        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE_FALSE(view.has_value());
        REQUIRE(
            view.error().code ==
            InitializedImageErrorCode::backing_size_mismatch);
    }

    SECTION("anonymous backing unsupported") {
        auto intent = zero_intent(0x1000, 1, 0);
        intent.backing.kind = MappingBackingKind::anonymous;
        std::array intents{intent};

        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE_FALSE(view.has_value());
        REQUIRE(
            view.error().code ==
            InitializedImageErrorCode::unsupported_backing_kind);
    }
}

TEST_CASE("file-backed and zero-fill bytes resolve correctly", "[memory][image-view]") {
    auto bytes = image({0xaa, 0xbb, 0xcc, 0xdd});
    std::array intents{
        file_intent(0x1000, 2, 1, 0),
        zero_intent(0x1002, 2, 1),
    };
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());

    REQUIRE(view->read_byte(GuestAddress{0x1000}).value() == std::byte{0xbb});
    REQUIRE(view->read_byte(GuestAddress{0x1001}).value() == std::byte{0xcc});
    REQUIRE(view->read_byte(GuestAddress{0x1002}).value() == std::byte{0});
    REQUIRE(view->read_byte(GuestAddress{0x1003}).value() == std::byte{0});

    auto missing = view->read_byte(GuestAddress{0x1004});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(
        missing.error().code ==
        InitializedImageErrorCode::unmapped_guest_address);
    REQUIRE(missing.error().guest_address == GuestAddress{0x1004});
}

TEST_CASE("final uint64 guest address is readable", "[memory][image-view]") {
    auto bytes = image({0x7a});
    const auto max = std::numeric_limits<std::uint64_t>::max();
    std::array intents{file_intent(max, 1, 0, 0)};

    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());
    REQUIRE(view->read_byte(GuestAddress{max}).value() == std::byte{0x7a});
}

TEST_CASE("compatible overlapping mappings resolve deterministically", "[memory][image-view]") {
    std::vector<std::byte> bytes(0x200, std::byte{0});
    bytes[0xa0] = std::byte{0x5a};

    SECTION("same file source with different permissions") {
        std::array intents{
            file_intent(0x1000, 0x100, 0x20, 9, 0x4),
            file_intent(0x1080, 0x80, 0xa0, 2, 0x5),
        };

        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());
        REQUIRE(view->read_byte(GuestAddress{0x1080}).value() == std::byte{0x5a});
    }

    SECTION("overlapping zero-fill") {
        std::array intents{
            zero_intent(0x2000, 0x100, 7),
            zero_intent(0x2080, 0x100, 1),
        };

        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());
        REQUIRE(view->read_byte(GuestAddress{0x2080}).value() == std::byte{0});
    }
}

TEST_CASE("conflicting overlapping sources are rejected on read", "[memory][image-view]") {
    auto bytes = image({0x00, 0x00, 0x00, 0x00});

    SECTION("file versus zero-fill conflicts even when file byte is zero") {
        std::array intents{
            file_intent(0x1000, 2, 0, 5),
            zero_intent(0x1001, 1, 3),
        };
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());

        auto value = view->read_byte(GuestAddress{0x1001});
        REQUIRE_FALSE(value.has_value());
        REQUIRE(
            value.error().code ==
            InitializedImageErrorCode::mapping_overlap_conflict);
        REQUIRE(value.error().first_source_index.has_value());
        REQUIRE(value.error().second_source_index.has_value());
    }

    SECTION("different file sources conflict even when byte values match") {
        std::array intents{
            file_intent(0x1000, 2, 0, 8),
            file_intent(0x1001, 1, 3, 4),
        };
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());

        auto value = view->read_byte(GuestAddress{0x1001});
        REQUIRE_FALSE(value.has_value());
        REQUIRE(
            value.error().code ==
            InitializedImageErrorCode::mapping_overlap_conflict);
    }
}

TEST_CASE("range reads cross mapping boundaries", "[memory][image-view]") {
    auto bytes = image({0x11, 0x22, 0x33, 0x44});

    SECTION("adjacent file-backed intents") {
        std::array intents{
            file_intent(0x1000, 2, 0, 0),
            file_intent(0x1002, 2, 2, 1),
        };
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());

        std::array<std::byte, 4> output{};
        auto copied = view->copy_bytes(range(0x1000, 4), output);
        REQUIRE(copied.has_value());
        REQUIRE(copied.value() == 4);
        REQUIRE(output[0] == std::byte{0x11});
        REQUIRE(output[1] == std::byte{0x22});
        REQUIRE(output[2] == std::byte{0x33});
        REQUIRE(output[3] == std::byte{0x44});
    }

    SECTION("file-backed into zero-fill") {
        std::array intents{
            file_intent(0x2000, 2, 0, 0),
            zero_intent(0x2002, 2, 1),
        };
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());

        std::array<std::byte, 4> output{};
        auto copied = view->copy_bytes(range(0x2000, 4), output);
        REQUIRE(copied.has_value());
        REQUIRE(output[0] == std::byte{0x11});
        REQUIRE(output[1] == std::byte{0x22});
        REQUIRE(output[2] == std::byte{0});
        REQUIRE(output[3] == std::byte{0});
    }

    SECTION("zero-fill into file-backed") {
        std::array intents{
            zero_intent(0x3000, 2, 0),
            file_intent(0x3002, 2, 2, 1),
        };
        auto view = InitializedImageView::create(bytes, intents);
        REQUIRE(view.has_value());

        std::array<std::byte, 4> output{};
        auto copied = view->copy_bytes(range(0x3000, 4), output);
        REQUIRE(copied.has_value());
        REQUIRE(output[0] == std::byte{0});
        REQUIRE(output[1] == std::byte{0});
        REQUIRE(output[2] == std::byte{0x33});
        REQUIRE(output[3] == std::byte{0x44});
    }
}

TEST_CASE("range read reports first failing guest address", "[memory][image-view]") {
    auto bytes = image({0x11, 0x22});
    std::array intents{
        file_intent(0x1000, 1, 0, 0),
        file_intent(0x1002, 1, 1, 1),
    };
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());

    std::array<std::byte, 3> output{};
    auto copied = view->copy_bytes(range(0x1000, 3), output);
    REQUIRE_FALSE(copied.has_value());
    REQUIRE(
        copied.error().code ==
        InitializedImageErrorCode::unmapped_guest_address);
    REQUIRE(copied.error().guest_address == GuestAddress{0x1001});
}

TEST_CASE("range read handles full-domain guest end", "[memory][image-view]") {
    auto bytes = image({0xab, 0xcd});
    const auto max = std::numeric_limits<std::uint64_t>::max();
    std::array intents{file_intent(max - 1, 2, 0, 0)};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());

    std::array<std::byte, 2> output{};
    auto copied = view->copy_bytes(range(max - 1, 2), output);
    REQUIRE(copied.has_value());
    REQUIRE(output[0] == std::byte{0xab});
    REQUIRE(output[1] == std::byte{0xcd});
}

TEST_CASE("range read validates output size", "[memory][image-view]") {
    auto bytes = image({0x11, 0x22});
    std::array intents{file_intent(0x1000, 2, 0, 0)};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());

    std::array<std::byte, 1> output{};
    auto copied = view->copy_bytes(range(0x1000, 2), output);
    REQUIRE_FALSE(copied.has_value());
    REQUIRE(
        copied.error().code ==
        InitializedImageErrorCode::output_size_mismatch);
}

TEST_CASE("empty range copies zero bytes", "[memory][image-view]") {
    auto bytes = image({0x11});
    std::array<MappingIntent, 0> intents{};
    auto view = InitializedImageView::create(bytes, intents);
    REQUIRE(view.has_value());

    std::array<std::byte, 0> output{};
    auto copied = view->copy_bytes(range(0x1234, 0), output);
    REQUIRE(copied.has_value());
    REQUIRE(copied.value() == 0);
}


TEST_CASE("mapping input order does not affect byte resolution", "[memory][image-view]") {
    std::vector<std::byte> bytes(0x200, std::byte{0});
    bytes[0xa0] = std::byte{0x6c};

    const auto broad = file_intent(0x1000, 0x100, 0x20, 9);
    const auto narrow = file_intent(0x1080, 0x80, 0xa0, 2);

    std::array forward{broad, narrow};
    std::array reverse{narrow, broad};

    auto forward_view = InitializedImageView::create(bytes, forward);
    auto reverse_view = InitializedImageView::create(bytes, reverse);

    REQUIRE(forward_view.has_value());
    REQUIRE(reverse_view.has_value());
    REQUIRE(
        forward_view->read_byte(GuestAddress{0x1080}).value() ==
        reverse_view->read_byte(GuestAddress{0x1080}).value());
    REQUIRE(forward_view->read_byte(GuestAddress{0x1080}).value() == std::byte{0x6c});
}
