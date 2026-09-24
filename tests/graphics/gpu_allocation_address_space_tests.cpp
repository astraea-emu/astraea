#include <astraea/graphics/gpu_allocation_address_space.hpp>

#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::GpuVirtualAddress gpu(
    std::uint64_t value) {
    return astraea::graphics::GpuVirtualAddress{
        .value = value,
    };
}

}  // namespace

TEST_CASE(
    "guest GPU allocations receive stable storage identities",
    "[graphics][gpu-allocation][registration]") {
    astraea::graphics::GuestGpuAllocationAddressSpace space;

    const auto first =
        space.register_allocation(gpu(0x1000U), 0x400U);
    const auto second =
        space.register_allocation(gpu(0x2000U), 0x800U);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->value == 0U);
    REQUIRE(second->value == 1U);
    REQUIRE(space.size() == 2U);

    const auto* first_entry = space.entry_at(first.value());
    REQUIRE(first_entry != nullptr);
    REQUIRE(first_entry->base.value == 0x1000U);
    REQUIRE(first_entry->byte_size == 0x400U);
}

TEST_CASE(
    "guest GPU allocation registration rejects overlap atomically",
    "[graphics][gpu-allocation][registration][negative]") {
    astraea::graphics::GuestGpuAllocationAddressSpace space;
    const auto first =
        space.register_allocation(gpu(0x1000U), 0x400U);
    REQUIRE(first.has_value());

    const auto overlap =
        space.register_allocation(gpu(0x1200U), 0x400U);

    REQUIRE_FALSE(overlap.has_value());
    REQUIRE(
        overlap.error().code ==
        astraea::graphics::
            GuestGpuAllocationRegistrationErrorCode::
                overlapping_allocation);
    REQUIRE(
        overlap.error().conflicting_allocation_id ==
        first.value());
    REQUIRE(space.size() == 1U);

    const auto adjacent =
        space.register_allocation(gpu(0x1400U), 0x100U);
    REQUIRE(adjacent.has_value());
    REQUIRE(adjacent->value == 1U);
}

TEST_CASE(
    "guest GPU allocation registration rejects zero and overflow",
    "[graphics][gpu-allocation][registration][negative]") {
    astraea::graphics::GuestGpuAllocationAddressSpace space;

    const auto zero =
        space.register_allocation(gpu(0x1000U), 0U);
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        astraea::graphics::
            GuestGpuAllocationRegistrationErrorCode::
                zero_sized_allocation);

    const auto overflow =
        space.register_allocation(
            gpu(std::numeric_limits<std::uint64_t>::max() - 3U),
            4U);
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(
        overflow.error().code ==
        astraea::graphics::
            GuestGpuAllocationRegistrationErrorCode::
                address_range_overflow);
}

TEST_CASE(
    "guest GPU allocation resolution returns identity and byte offset",
    "[graphics][gpu-allocation][resolve]") {
    astraea::graphics::GuestGpuAllocationAddressSpace space;
    const auto id =
        space.register_allocation(gpu(0x1000U), 0x1000U);
    REQUIRE(id.has_value());

    const auto result =
        space.resolve_range(gpu(0x1200U), 0x400U);

    REQUIRE(result.has_value());
    REQUIRE(result->allocation_id == id.value());
    REQUIRE(result->byte_offset == 0x200U);
    REQUIRE(result->byte_count == 0x400U);
}

TEST_CASE(
    "guest GPU allocation resolution never stitches adjacent storage",
    "[graphics][gpu-allocation][resolve][negative]") {
    astraea::graphics::GuestGpuAllocationAddressSpace space;
    REQUIRE(
        space.register_allocation(gpu(0x1000U), 0x400U)
            .has_value());
    REQUIRE(
        space.register_allocation(gpu(0x1400U), 0x400U)
            .has_value());

    const auto result =
        space.resolve_range(gpu(0x1300U), 0x200U);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                partially_mapped_range);
    REQUIRE(
        result.error().intersecting_allocation_id.has_value());
}

TEST_CASE(
    "guest GPU allocation resolution distinguishes unmapped zero and overflow",
    "[graphics][gpu-allocation][resolve][negative]") {
    astraea::graphics::GuestGpuAllocationAddressSpace space;
    REQUIRE(
        space.register_allocation(gpu(0x1000U), 0x400U)
            .has_value());

    const auto unmapped =
        space.resolve_range(gpu(0x3000U), 0x100U);
    REQUIRE_FALSE(unmapped.has_value());
    REQUIRE(
        unmapped.error().code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                unmapped_range);

    const auto zero =
        space.resolve_range(gpu(0x1000U), 0U);
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                zero_sized_request);

    const auto overflow =
        space.resolve_range(
            gpu(std::numeric_limits<std::uint64_t>::max() - 3U),
            4U);
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(
        overflow.error().code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                address_range_overflow);
}
