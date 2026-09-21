#include <astraea/memory/mapping.hpp>

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::loader::ProgramHeader load(
    std::uint32_t flags,
    std::uint64_t file_offset,
    std::uint64_t virtual_address,
    std::uint64_t file_size,
    std::uint64_t memory_size,
    std::size_t index = 0) {
    return astraea::loader::ProgramHeader{
        .type = 1,
        .flags = flags,
        .offset = file_offset,
        .virtual_address = virtual_address,
        .physical_address = 0,
        .file_size = file_size,
        .memory_size = memory_size,
        .alignment = 0,
        .index = index,
    };
}

}  // namespace

TEST_CASE("ELF permission flags map independently to guest permissions", "[memory][mapping]") {
    for (std::uint32_t flags = 0; flags < 8; ++flags) {
        const auto permissions = astraea::memory::GuestPermissions::from_elf_flags(flags);

        REQUIRE(permissions.has(astraea::memory::GuestPermission::read) == ((flags & 0x4U) != 0));
        REQUIRE(permissions.has(astraea::memory::GuestPermission::write) == ((flags & 0x2U) != 0));
        REQUIRE(
            permissions.has(astraea::memory::GuestPermission::execute) == ((flags & 0x1U) != 0));
    }
}

TEST_CASE("raw permission construction rejects unknown bits", "[memory][mapping]") {
    auto valid = astraea::memory::GuestPermissions::checked_from_bits(0x7U);
    REQUIRE(valid.has_value());

    auto invalid = astraea::memory::GuestPermissions::checked_from_bits(0x8U);
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(
        invalid.error().code ==
        astraea::memory::PermissionErrorCode::invalid_permission_bits);
}

TEST_CASE("PT_LOAD with equal file and memory sizes yields one file intent", "[memory][mapping]") {
    const auto ph = load(0x5, 0x100, 0x400100, 0x20, 0x20, 3);
    auto result = astraea::memory::mapping_intents_from_load(ph);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    const auto& intent = result->front();
    REQUIRE(intent.range.base().value() == 0x400100);
    REQUIRE(intent.range.size().value() == 0x20);
    REQUIRE(intent.backing.kind == astraea::memory::MappingBackingKind::file);
    REQUIRE(intent.backing.file_offset == 0x100);
    REQUIRE(intent.source_index == 3);
}

TEST_CASE("PT_LOAD zero-fill tail becomes a separate adjacent intent", "[memory][mapping]") {
    const auto ph = load(0x6, 0x200, 0x500200, 0x20, 0x80, 4);
    auto result = astraea::memory::mapping_intents_from_load(ph);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    REQUIRE((*result)[0].backing.kind == astraea::memory::MappingBackingKind::file);
    REQUIRE((*result)[0].range.base().value() == 0x500200);
    REQUIRE((*result)[0].range.size().value() == 0x20);
    REQUIRE((*result)[1].backing.kind == astraea::memory::MappingBackingKind::zero_fill);
    REQUIRE((*result)[1].range.base().value() == 0x500220);
    REQUIRE((*result)[1].range.size().value() == 0x60);
    REQUIRE_FALSE((*result)[0].range.overlaps((*result)[1].range));
}

TEST_CASE("PT_LOAD with zero file size yields only zero-fill intent", "[memory][mapping]") {
    const auto ph = load(0x6, 0, 0x7000, 0, 0x40);
    auto result = astraea::memory::mapping_intents_from_load(ph);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);
    REQUIRE(result->front().backing.kind == astraea::memory::MappingBackingKind::zero_fill);
    REQUIRE(result->front().range.base().value() == 0x7000);
    REQUIRE(result->front().range.size().value() == 0x40);
}

TEST_CASE("zero-sized PT_LOAD produces no mapping intent", "[memory][mapping]") {
    const auto ph = load(0, 0, 0x8000, 0, 0);
    auto result = astraea::memory::mapping_intents_from_load(ph);

    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

TEST_CASE("mapping transformation defensively rejects invalid load metadata", "[memory][mapping]") {
    SECTION("not PT_LOAD") {
        auto ph = load(0, 0, 0, 0, 0);
        ph.type = 0;
        auto result = astraea::memory::mapping_intents_from_load(ph);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::memory::MappingErrorCode::not_load_segment);
    }

    SECTION("file size exceeds memory size") {
        auto result = astraea::memory::mapping_intents_from_load(load(0, 0, 0, 2, 1));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::memory::MappingErrorCode::invalid_load_sizes);
    }

    SECTION("guest range overflow") {
        const auto max = std::numeric_limits<std::uint64_t>::max();
        auto result = astraea::memory::mapping_intents_from_load(load(0, 0, max, 0, 2));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code == astraea::memory::MappingErrorCode::guest_range_overflow);
    }
}

TEST_CASE("mapping overlap classification exposes disagreements", "[memory][mapping]") {
    const auto make_intent = [](
                                 std::uint64_t base,
                                 std::uint64_t size,
                                 astraea::memory::MappingBackingKind kind,
                                 std::uint64_t file_offset,
                                 std::uint8_t permissions,
                                 std::size_t source) {
        auto range = astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{base},
            astraea::memory::GuestSize{size});
        REQUIRE(range.has_value());
        return astraea::memory::MappingIntent{
            .range = range.value(),
            .permissions = astraea::memory::GuestPermissions::checked_from_bits(permissions).value(),
            .backing =
                astraea::memory::MappingBacking{
                    .kind = kind,
                    .file_offset = file_offset,
                    .byte_count = astraea::memory::GuestSize{size},
                },
            .source_index = source,
        };
    };

    auto file_a = make_intent(0x1000, 0x100, astraea::memory::MappingBackingKind::file, 0x100, 1, 0);
    auto identical =
        make_intent(0x1000, 0x100, astraea::memory::MappingBackingKind::file, 0x100, 1, 0);
    auto other_file =
        make_intent(0x1080, 0x100, astraea::memory::MappingBackingKind::file, 0x300, 1, 1);
    auto same_source_shifted =
        make_intent(0x1080, 0x80, astraea::memory::MappingBackingKind::file, 0x180, 1, 5);
    auto zero =
        make_intent(0x1080, 0x100, astraea::memory::MappingBackingKind::zero_fill, 0, 1, 2);
    auto different_perms =
        make_intent(0x1080, 0x100, astraea::memory::MappingBackingKind::file, 0x100, 3, 3);
    auto adjacent =
        make_intent(0x1100, 0x100, astraea::memory::MappingBackingKind::file, 0x200, 1, 4);

    REQUIRE(
        astraea::memory::classify_overlap(file_a, identical) ==
        astraea::memory::OverlapClass::identical_compatible);
    REQUIRE(
        astraea::memory::classify_overlap(file_a, other_file) ==
        astraea::memory::OverlapClass::conflicting_initialized_bytes);
    REQUIRE(
        astraea::memory::classify_overlap(file_a, same_source_shifted) ==
        astraea::memory::OverlapClass::different_backing);
    REQUIRE(
        astraea::memory::classify_overlap(file_a, zero) ==
        astraea::memory::OverlapClass::file_vs_zero_fill);
    REQUIRE(
        astraea::memory::classify_overlap(file_a, different_perms) ==
        astraea::memory::OverlapClass::permission_disagreement);
    REQUIRE(
        astraea::memory::classify_overlap(file_a, adjacent) ==
        astraea::memory::OverlapClass::none);
}
