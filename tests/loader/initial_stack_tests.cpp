#include <astraea/loader/initial_stack.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::loader::AuxiliaryVectorEntry;
using astraea::loader::InitialStackErrorCode;
using astraea::loader::InitialStackInputKind;
using astraea::loader::InitialStackRequest;
using astraea::memory::GuestAddress;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;

GuestRange range(std::uint64_t base, std::uint64_t size) {
    auto result = GuestRange::create(GuestAddress{base}, GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

std::uint64_t read_u64(
    const std::vector<std::byte>& bytes,
    std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + i]))
                 << (i * 8U);
    }
    return value;
}

std::string read_c_string(
    const astraea::loader::InitialStackImage& image,
    std::uint64_t guest_address) {
    REQUIRE(guest_address >= image.used_range.base().value());
    const auto offset_u64 =
        guest_address - image.used_range.base().value();
    REQUIRE(offset_u64 < image.bytes.size());

    std::string result;
    auto offset = static_cast<std::size_t>(offset_u64);
    while (offset < image.bytes.size() && image.bytes[offset] != std::byte{0}) {
        result.push_back(static_cast<char>(
            std::to_integer<unsigned char>(image.bytes[offset])));
        ++offset;
    }
    REQUIRE(offset < image.bytes.size());
    return result;
}

InitialStackRequest request(
    GuestRange storage,
    std::vector<std::string> arguments = {},
    std::vector<std::string> environment = {},
    std::vector<AuxiliaryVectorEntry> auxiliary = {}) {
    return InitialStackRequest{
        .storage = storage,
        .arguments = std::move(arguments),
        .environment = std::move(environment),
        .auxiliary_vector = std::move(auxiliary),
    };
}

}  // namespace

TEST_CASE("initial stack serializes generic x86-64 process state", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(
            range(0x100000, 0x1000),
            {"probe", "alpha"},
            {"A=B"},
            {{3, 0x1234}, {9, 0x5678}}));

    REQUIRE(result.has_value());
    REQUIRE((result->rsp.value() % 16U) == 0);
    REQUIRE(result->used_range.base() == result->rsp);
    REQUIRE(result->bytes.size() == result->used_range.size().value());

    std::size_t cursor = 0;
    REQUIRE(read_u64(result->bytes, cursor) == 2);
    cursor += 8;

    const auto argv0 = read_u64(result->bytes, cursor);
    cursor += 8;
    const auto argv1 = read_u64(result->bytes, cursor);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 0);
    cursor += 8;

    const auto env0 = read_u64(result->bytes, cursor);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 0);
    cursor += 8;

    REQUIRE(read_u64(result->bytes, cursor) == 3);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 0x1234);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 9);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 0x5678);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 0);
    cursor += 8;
    REQUIRE(read_u64(result->bytes, cursor) == 0);

    REQUIRE(read_c_string(result.value(), argv0) == "probe");
    REQUIRE(read_c_string(result.value(), argv1) == "alpha");
    REQUIRE(read_c_string(result.value(), env0) == "A=B");
    REQUIRE(argv0 < argv1);
    REQUIRE(argv1 < env0);
}

TEST_CASE("empty argv env and auxv produce valid minimal stack", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(range(0x200000, 0x100)));

    REQUIRE(result.has_value());
    REQUIRE((result->rsp.value() % 16U) == 0);

    REQUIRE(read_u64(result->bytes, 0) == 0);
    REQUIRE(read_u64(result->bytes, 8) == 0);
    REQUIRE(read_u64(result->bytes, 16) == 0);
    REQUIRE(read_u64(result->bytes, 24) == 0);
    REQUIRE(read_u64(result->bytes, 32) == 0);
}

TEST_CASE("initial stack rejects embedded NUL bytes", "[loader][initial-stack]") {
    SECTION("argument") {
        auto result = astraea::loader::build_initial_stack(
            request(
                range(0x300000, 0x100),
                {std::string{"a\0b", 3}}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == InitialStackErrorCode::embedded_nul);
        REQUIRE(result.error().input_kind == InitialStackInputKind::argument);
        REQUIRE(result.error().input_index == std::size_t{0});
    }

    SECTION("environment") {
        auto result = astraea::loader::build_initial_stack(
            request(
                range(0x300000, 0x100),
                {},
                {std::string{"A\0B", 3}}));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == InitialStackErrorCode::embedded_nul);
        REQUIRE(result.error().input_kind == InitialStackInputKind::environment);
        REQUIRE(result.error().input_index == std::size_t{0});
    }
}

TEST_CASE("caller may not supply AT_NULL", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(
            range(0x400000, 0x100),
            {},
            {},
            {{0, 123}}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        InitialStackErrorCode::auxv_contains_terminator);
    REQUIRE(
        result.error().input_kind ==
        InitialStackInputKind::auxiliary_vector);
}

TEST_CASE("initial stack fails when storage is too small", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(range(0x500000, 39)));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == InitialStackErrorCode::stack_too_small);
}

TEST_CASE("minimal stack can exactly fit aligned storage", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(range(0x600000, 40)));

    REQUIRE(result.has_value());
    REQUIRE(result->rsp == GuestAddress{0x600000});
    REQUIRE(result->bytes.size() == 40);
}

TEST_CASE("initial stack inserts deterministic alignment padding", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(range(0x700003, 64)));

    REQUIRE(result.has_value());
    REQUIRE((result->rsp.value() % 16U) == 0);
    REQUIRE(result->used_range.size().value() > 40);
    REQUIRE(result->bytes.size() == result->used_range.size().value());
}

TEST_CASE("initial stack supports storage ending at UINT64_MAX", "[loader][initial-stack]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    auto result = astraea::loader::build_initial_stack(
        request(
            range(max - 127U, 128),
            {"z"}));

    REQUIRE(result.has_value());
    REQUIRE((result->rsp.value() % 16U) == 0);
    REQUIRE(result->used_range.contains(GuestAddress{max}));
    REQUIRE(read_c_string(
                result.value(),
                read_u64(result->bytes, 8)) == "z");
}

TEST_CASE("huge guest capacity does not cause capacity-sized allocation", "[loader][initial-stack]") {
    const auto huge = std::numeric_limits<std::uint64_t>::max();
    auto result = astraea::loader::build_initial_stack(
        request(
            range(0, huge),
            {"probe"}));

    REQUIRE(result.has_value());
    REQUIRE(result->bytes.size() < 256);
    REQUIRE(result->used_range.size().value() == result->bytes.size());
}

TEST_CASE("stack construction is deterministic", "[loader][initial-stack]") {
    const auto make_request = [] {
        return request(
            range(0x800000, 0x400),
            {"probe", ""},
            {"A=B", "C=D"},
            {{3, 0x1111}});
    };

    auto first = astraea::loader::build_initial_stack(make_request());
    auto second = astraea::loader::build_initial_stack(make_request());

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->rsp == second->rsp);
    REQUIRE(first->used_range == second->used_range);
    REQUIRE(first->bytes == second->bytes);
}

TEST_CASE("all serialized stack words are little endian", "[loader][initial-stack]") {
    auto result = astraea::loader::build_initial_stack(
        request(
            range(0x900000, 0x200),
            {"x"},
            {},
            {{0x0102030405060708ULL, 0x1112131415161718ULL}}));

    REQUIRE(result.has_value());
    REQUIRE(result->bytes[0] == std::byte{0x01});
    REQUIRE(result->bytes[1] == std::byte{0x00});

    // argc is 1; after argc, argv[0], argv-null, env-null:
    // the first auxv type begins at byte 32.
    REQUIRE(result->bytes[32] == std::byte{0x08});
    REQUIRE(result->bytes[33] == std::byte{0x07});
    REQUIRE(result->bytes[39] == std::byte{0x01});
    REQUIRE(result->bytes[40] == std::byte{0x18});
    REQUIRE(result->bytes[47] == std::byte{0x11});
}
