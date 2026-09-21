#include <astraea/loader/tls_template.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::ProgramHeader;
using astraea::loader::TlsTemplateDescriptor;
using astraea::loader::TlsTemplateErrorCode;
using astraea::memory::GuestAddress;
using astraea::memory::GuestSize;

constexpr std::uint32_t kPtTls = 7;
constexpr std::uint32_t kPfRead = 0x4;

ProgramHeader tls_header(
    std::uint64_t offset,
    std::uint64_t virtual_address,
    std::uint64_t file_size,
    std::uint64_t memory_size,
    std::uint64_t alignment,
    std::size_t index = 0,
    std::uint32_t flags = kPfRead) {
    return ProgramHeader{
        .type = kPtTls,
        .flags = flags,
        .offset = offset,
        .virtual_address = virtual_address,
        .physical_address = 0,
        .file_size = file_size,
        .memory_size = memory_size,
        .alignment = alignment,
        .index = index,
    };
}

ProgramHeader other_header(std::size_t index = 0) {
    auto header = tls_header(0, 0, 0, 0, 0, index);
    header.type = 1;
    return header;
}

}  // namespace

TEST_CASE("TLS metadata may be absent", "[loader][tls]") {
    std::array headers{other_header()};
    std::array<std::byte, 0> image{};

    auto result =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
}

TEST_CASE("TLS descriptor preserves initialized-only template metadata", "[loader][tls]") {
    const std::array<std::byte, 4> image{
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30},
        std::byte{0x40},
    };
    std::array headers{tls_header(1, 0x1001, 3, 3, 1, 5)};

    auto result =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    REQUIRE(result->value().source_program_header_index == 5);
    REQUIRE(result->value().file_offset == 1);
    REQUIRE(result->value().initialization_address == GuestAddress{0x1001});
    REQUIRE(result->value().initialized_size == GuestSize{3});
    REQUIRE(result->value().total_size == GuestSize{3});
    REQUIRE(result->value().alignment == 1);

    auto materialized =
        astraea::loader::materialize_tls_template(result->value(), image);
    REQUIRE(materialized.has_value());
    REQUIRE(
        materialized.value() ==
        std::vector<std::byte>{
            std::byte{0x20},
            std::byte{0x30},
            std::byte{0x40},
        });
}

TEST_CASE("TLS materializer zero-fills bytes after initialization image", "[loader][tls]") {
    const std::array<std::byte, 3> image{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xcc},
    };
    std::array headers{tls_header(1, 0x2001, 2, 5, 1)};

    auto descriptor =
        astraea::loader::build_tls_template_descriptor(headers, image);
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());

    auto materialized =
        astraea::loader::materialize_tls_template(
            descriptor->value(), image);

    REQUIRE(materialized.has_value());
    REQUIRE(materialized->size() == 5);
    REQUIRE((*materialized)[0] == std::byte{0xbb});
    REQUIRE((*materialized)[1] == std::byte{0xcc});
    REQUIRE((*materialized)[2] == std::byte{0});
    REQUIRE((*materialized)[3] == std::byte{0});
    REQUIRE((*materialized)[4] == std::byte{0});
}

TEST_CASE("TLS template may be entirely zero-filled", "[loader][tls]") {
    std::array<std::byte, 0> image{};
    std::array headers{
        tls_header(
            std::numeric_limits<std::uint64_t>::max(),
            0x3000,
            0,
            4,
            0)};

    auto descriptor =
        astraea::loader::build_tls_template_descriptor(headers, image);
    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());

    auto materialized =
        astraea::loader::materialize_tls_template(
            descriptor->value(), image);
    REQUIRE(materialized.has_value());
    REQUIRE(
        materialized.value() ==
        std::vector<std::byte>(4, std::byte{0}));
}

TEST_CASE("zero-size TLS template preserves otherwise-unused file offset", "[loader][tls]") {
    std::array<std::byte, 0> image{};
    const auto max = std::numeric_limits<std::uint64_t>::max();
    std::array headers{
        tls_header(max, max, 0, 0, 0, 3)};

    auto descriptor =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());
    REQUIRE(descriptor->value().file_offset == max);
    REQUIRE(
        descriptor->value().initialization_address ==
        GuestAddress{max});

    auto materialized =
        astraea::loader::materialize_tls_template(
            descriptor->value(), image);
    REQUIRE(materialized.has_value());
    REQUIRE(materialized->empty());
}

TEST_CASE("duplicate PT_TLS segments are rejected", "[loader][tls]") {
    std::array headers{
        tls_header(0, 0x1000, 0, 0, 0, 2),
        tls_header(0, 0x2000, 0, 0, 0, 7),
    };
    std::array<std::byte, 0> image{};

    auto result =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        TlsTemplateErrorCode::duplicate_tls_segment);
    REQUIRE(result.error().source_program_header_index == std::size_t{7});
    REQUIRE(
        result.error().conflicting_program_header_index ==
        std::size_t{2});
}

TEST_CASE("TLS initialized size may not exceed total size", "[loader][tls]") {
    std::array headers{tls_header(0, 0x1000, 2, 1, 1, 4)};
    std::array<std::byte, 2> image{};

    auto result =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        TlsTemplateErrorCode::tls_initialized_size_exceeds_total);
    REQUIRE(result.error().source_program_header_index == std::size_t{4});
}

TEST_CASE("TLS file range is checked without wrapping", "[loader][tls]") {
    SECTION("overflow") {
        const auto max = std::numeric_limits<std::uint64_t>::max();
        std::array headers{tls_header(max, 0x1000, 2, 2, 1)};
        std::array<std::byte, 2> image{};

        auto result =
            astraea::loader::build_tls_template_descriptor(headers, image);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TlsTemplateErrorCode::tls_file_range_overflow);
    }

    SECTION("past EOF") {
        std::array headers{tls_header(1, 0x1001, 2, 2, 1)};
        std::array<std::byte, 2> image{};

        auto result =
            astraea::loader::build_tls_template_descriptor(headers, image);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TlsTemplateErrorCode::tls_file_range_out_of_bounds);
    }
}

TEST_CASE("TLS requires generic read-only flags", "[loader][tls]") {
    std::array<std::byte, 0> image{};

    for (const auto flags :
         std::array<std::uint32_t, 4>{0U, 1U, 2U, 6U}) {
        DYNAMIC_SECTION("flags " << flags) {
            std::array headers{
                tls_header(0, 0x1000, 0, 0, 0, 0, flags)};

            auto result =
                astraea::loader::build_tls_template_descriptor(
                    headers, image);

            REQUIRE_FALSE(result.has_value());
            REQUIRE(
                result.error().code ==
                TlsTemplateErrorCode::invalid_tls_flags);
        }
    }
}

TEST_CASE("TLS alignment accepts zero one and valid powers of two", "[loader][tls]") {
    std::array<std::byte, 64> image{};

    SECTION("zero") {
        std::array headers{tls_header(3, 0x1005, 1, 1, 0)};
        REQUIRE(
            astraea::loader::build_tls_template_descriptor(
                headers, image)
                .has_value());
    }

    SECTION("one") {
        std::array headers{tls_header(3, 0x1005, 1, 1, 1)};
        REQUIRE(
            astraea::loader::build_tls_template_descriptor(
                headers, image)
                .has_value());
    }

    SECTION("power of two with congruence") {
        std::array headers{tls_header(8, 0x1008, 1, 1, 8)};
        REQUIRE(
            astraea::loader::build_tls_template_descriptor(
                headers, image)
                .has_value());
    }
}

TEST_CASE("TLS rejects invalid alignment and congruence", "[loader][tls]") {
    std::array<std::byte, 64> image{};

    SECTION("non power of two") {
        std::array headers{tls_header(0, 0x1000, 1, 1, 3)};
        auto result =
            astraea::loader::build_tls_template_descriptor(
                headers, image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TlsTemplateErrorCode::invalid_tls_alignment);
    }

    SECTION("congruence mismatch") {
        std::array headers{tls_header(4, 0x1000, 1, 1, 8)};
        auto result =
            astraea::loader::build_tls_template_descriptor(
                headers, image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TlsTemplateErrorCode::invalid_tls_alignment_congruence);
    }
}

TEST_CASE("TLS descriptor construction does not allocate total template size", "[loader][tls]") {
    const auto huge = std::numeric_limits<std::uint64_t>::max();
    std::array<std::byte, 0> image{};
    std::array headers{tls_header(huge, huge, 0, huge, 0)};

    auto descriptor =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE(descriptor.has_value());
    REQUIRE(descriptor->has_value());
    REQUIRE(descriptor->value().total_size == GuestSize{huge});

    auto materialized =
        astraea::loader::materialize_tls_template(
            descriptor->value(), image);
    REQUIRE_FALSE(materialized.has_value());
    REQUIRE(
        materialized.error().code ==
        TlsTemplateErrorCode::host_size_unrepresentable);
}

TEST_CASE("TLS materializer defensively revalidates descriptor source range", "[loader][tls]") {
    const TlsTemplateDescriptor descriptor{
        .source_program_header_index = 8,
        .file_offset = 1,
        .initialization_address = GuestAddress{0x1001},
        .initialized_size = GuestSize{2},
        .total_size = GuestSize{2},
        .alignment = 1,
    };
    std::array<std::byte, 2> image{};

    auto result =
        astraea::loader::materialize_tls_template(
            descriptor, image);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        TlsTemplateErrorCode::tls_file_range_out_of_bounds);
    REQUIRE(result.error().source_program_header_index == std::size_t{8});
}

TEST_CASE("TLS descriptor and materialization are deterministic", "[loader][tls]") {
    const std::array<std::byte, 4> image{
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
    };
    std::array headers{tls_header(0, 0x4000, 4, 8, 0)};

    auto first =
        astraea::loader::build_tls_template_descriptor(headers, image);
    auto second =
        astraea::loader::build_tls_template_descriptor(headers, image);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->has_value());
    REQUIRE(second->has_value());
    REQUIRE(first->value() == second->value());

    auto first_bytes =
        astraea::loader::materialize_tls_template(
            first->value(), image);
    auto second_bytes =
        astraea::loader::materialize_tls_template(
            second->value(), image);

    REQUIRE(first_bytes.has_value());
    REQUIRE(second_bytes.has_value());
    REQUIRE(first_bytes.value() == second_bytes.value());
}
