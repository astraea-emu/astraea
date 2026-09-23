#include <astraea/graphics/gpu_buffer_address_space.hpp>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

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
    "guest GPU buffer registration assigns stable logical IDs",
    "[graphics][gpu-address-space][registration]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;

    const auto first =
        space.register_buffer(
            gpu(0x1000U),
            0x100U);
    const auto second =
        space.register_buffer(
            gpu(0x2000U),
            0x80U);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->value == 0U);
    REQUIRE(second->value == 1U);
    REQUIRE(space.size() == 2U);

    const auto* first_entry =
        space.entry_at(first.value());
    const auto* second_entry =
        space.entry_at(second.value());
    REQUIRE(first_entry != nullptr);
    REQUIRE(second_entry != nullptr);
    REQUIRE(first_entry->base.value == 0x1000U);
    REQUIRE(first_entry->byte_size == 0x100U);
    REQUIRE(second_entry->base.value == 0x2000U);
    REQUIRE(second_entry->byte_size == 0x80U);
}

TEST_CASE(
    "guest GPU buffer IDs survive registry storage growth",
    "[graphics][gpu-address-space][registration]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;

    const auto first =
        space.register_buffer(
            gpu(0x1000U),
            0x100U);
    REQUIRE(first.has_value());

    for (std::uint64_t index = 0;
         index < 128U;
         ++index) {
        const auto result =
            space.register_buffer(
                gpu(0x10000U + index * 0x100U),
                0x80U);
        REQUIRE(result.has_value());
    }

    const auto* entry =
        space.entry_at(first.value());
    REQUIRE(entry != nullptr);
    REQUIRE(entry->id == first.value());
    REQUIRE(entry->base.value == 0x1000U);
    REQUIRE(entry->byte_size == 0x100U);
}

TEST_CASE(
    "adjacent guest GPU buffers are accepted",
    "[graphics][gpu-address-space][registration]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;

    const auto left =
        space.register_buffer(
            gpu(0x1000U),
            0x100U);
    const auto right =
        space.register_buffer(
            gpu(0x1100U),
            0x100U);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->value == 0U);
    REQUIRE(right->value == 1U);
}

TEST_CASE(
    "guest GPU buffer overlap is rejected atomically",
    "[graphics][gpu-address-space][registration][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    const auto first =
        space.register_buffer(
            gpu(0x1000U),
            0x100U);
    REQUIRE(first.has_value());

    for (const auto& [base, size] :
         std::vector<std::pair<std::uint64_t, std::uint64_t>>{
             {0x1000U, 0x100U},
             {0x0ff0U, 0x20U},
             {0x10f0U, 0x20U},
             {0x1020U, 0x20U},
             {0x0f00U, 0x300U},
         }) {
        const auto result =
            space.register_buffer(
                gpu(base),
                size);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuBufferRegistrationErrorCode::
                    overlapping_region);
        REQUIRE(
            result.error().conflicting_buffer_id ==
            first.value());
        REQUIRE(space.size() == 1U);
    }

    const auto second =
        space.register_buffer(
            gpu(0x2000U),
            0x100U);
    REQUIRE(second.has_value());
    REQUIRE(second->value == 1U);
}

TEST_CASE(
    "zero-sized and overflowing guest GPU registrations fail",
    "[graphics][gpu-address-space][registration][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;

    const auto zero =
        space.register_buffer(
            gpu(0x1000U),
            0U);
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        astraea::graphics::
            GuestGpuBufferRegistrationErrorCode::
                zero_sized_region);

    const auto overflow =
        space.register_buffer(
            gpu(
                std::numeric_limits<std::uint64_t>::max() -
                3U),
            4U);
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(
        overflow.error().code ==
        astraea::graphics::
            GuestGpuBufferRegistrationErrorCode::
                address_range_overflow);

    REQUIRE(space.size() == 0U);
}

TEST_CASE(
    "guest GPU range resolution handles exact and interior ranges",
    "[graphics][gpu-address-space][resolve]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    const auto id =
        space.register_buffer(
            gpu(0x1000U),
            0x100U);
    REQUIRE(id.has_value());

    const auto exact =
        space.resolve_range(
            gpu(0x1000U),
            0x100U);
    REQUIRE(exact.has_value());
    REQUIRE(exact->buffer_id == id.value());
    REQUIRE(exact->byte_offset == 0U);
    REQUIRE(exact->byte_count == 0x100U);

    const auto interior =
        space.resolve_range(
            gpu(0x1040U),
            0x20U);
    REQUIRE(interior.has_value());
    REQUIRE(interior->buffer_id == id.value());
    REQUIRE(interior->byte_offset == 0x40U);
    REQUIRE(interior->byte_count == 0x20U);

    const auto ending_at_end =
        space.resolve_range(
            gpu(0x10f0U),
            0x10U);
    REQUIRE(ending_at_end.has_value());
    REQUIRE(
        ending_at_end->byte_offset ==
        0xf0U);
}

TEST_CASE(
    "range starting exactly at buffer end is unmapped",
    "[graphics][gpu-address-space][resolve][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    REQUIRE(
        space.register_buffer(
                 gpu(0x1000U),
                 0x100U)
            .has_value());

    const auto result =
        space.resolve_range(
            gpu(0x1100U),
            4U);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                unmapped_range);
    REQUIRE_FALSE(
        result.error().intersecting_buffer_id.has_value());
}

TEST_CASE(
    "fully unmapped guest GPU request fails explicitly",
    "[graphics][gpu-address-space][resolve][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    REQUIRE(
        space.register_buffer(
                 gpu(0x1000U),
                 0x100U)
            .has_value());

    const auto result =
        space.resolve_range(
            gpu(0x3000U),
            0x20U);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                unmapped_range);
}

TEST_CASE(
    "partially mapped guest GPU requests fail explicitly",
    "[graphics][gpu-address-space][resolve][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    const auto id =
        space.register_buffer(
            gpu(0x1000U),
            0x100U);
    REQUIRE(id.has_value());

    const auto extends_right =
        space.resolve_range(
            gpu(0x10f0U),
            0x20U);
    REQUIRE_FALSE(extends_right.has_value());
    REQUIRE(
        extends_right.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                partially_mapped_range);
    REQUIRE(
        extends_right.error().intersecting_buffer_id ==
        id.value());

    const auto enters_left =
        space.resolve_range(
            gpu(0x0ff0U),
            0x20U);
    REQUIRE_FALSE(enters_left.has_value());
    REQUIRE(
        enters_left.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                partially_mapped_range);
    REQUIRE(
        enters_left.error().intersecting_buffer_id ==
        id.value());
}

TEST_CASE(
    "guest GPU request spanning adjacent buffers is not stitched",
    "[graphics][gpu-address-space][resolve][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    REQUIRE(
        space.register_buffer(
                 gpu(0x1000U),
                 0x10U)
            .has_value());
    REQUIRE(
        space.register_buffer(
                 gpu(0x1010U),
                 0x10U)
            .has_value());

    const auto result =
        space.resolve_range(
            gpu(0x100cU),
            8U);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                partially_mapped_range);
}

TEST_CASE(
    "zero-sized and overflowing guest GPU requests fail",
    "[graphics][gpu-address-space][resolve][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;

    const auto zero =
        space.resolve_range(
            gpu(0x1000U),
            0U);
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                zero_sized_request);

    const auto overflow =
        space.resolve_range(
            gpu(
                std::numeric_limits<std::uint64_t>::max() -
                3U),
            4U);
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(
        overflow.error().code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                address_range_overflow);
}

TEST_CASE(
    "W0 GPU memory write resolves payload byte range without mutation",
    "[graphics][gpu-address-space][write-data]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    const auto id =
        space.register_buffer(
            gpu(0x4000U),
            0x100U);
    REQUIRE(id.has_value());
    const auto before_size = space.size();

    const astraea::graphics::GraphicsIrGpuMemoryWrite one{
        .destination = gpu(0x4020U),
        .values =
            std::vector<std::uint32_t>{
                0xdeadbeefU},
    };
    const auto one_result =
        astraea::graphics::resolve_gpu_memory_write(
            one,
            space);
    REQUIRE(one_result.has_value());
    REQUIRE(one_result->buffer_id == id.value());
    REQUIRE(one_result->byte_offset == 0x20U);
    REQUIRE(one_result->byte_count == 4U);

    const astraea::graphics::GraphicsIrGpuMemoryWrite many{
        .destination = gpu(0x4040U),
        .values =
            std::vector<std::uint32_t>{
                1U,
                2U,
                3U,
                4U,
            },
    };
    const auto many_result =
        astraea::graphics::resolve_gpu_memory_write(
            many,
            space);
    REQUIRE(many_result.has_value());
    REQUIRE(many_result->buffer_id == id.value());
    REQUIRE(many_result->byte_offset == 0x40U);
    REQUIRE(many_result->byte_count == 16U);

    REQUIRE(space.size() == before_size);
    const auto* entry = space.entry_at(id.value());
    REQUIRE(entry != nullptr);
    REQUIRE(entry->base.value == 0x4000U);
    REQUIRE(entry->byte_size == 0x100U);
}

TEST_CASE(
    "W0 GPU memory write preserves typed address resolution failure",
    "[graphics][gpu-address-space][write-data][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    REQUIRE(
        space.register_buffer(
                 gpu(0x4000U),
                 4U)
            .has_value());

    const astraea::graphics::GraphicsIrGpuMemoryWrite operation{
        .destination = gpu(0x4000U),
        .values =
            std::vector<std::uint32_t>{
                1U,
                2U,
            },
    };

    const auto result =
        astraea::graphics::resolve_gpu_memory_write(
            operation,
            space);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GpuMemoryWriteResolutionErrorCode::
                address_resolution_failure);
    REQUIRE(
        result.error().address_resolution_error.has_value());
    REQUIRE(
        result.error().address_resolution_error->code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                partially_mapped_range);
}

TEST_CASE(
    "empty GPU memory write remains a zero-sized resolution failure",
    "[graphics][gpu-address-space][write-data][negative]") {
    astraea::graphics::GuestGpuBufferAddressSpace space;
    REQUIRE(
        space.register_buffer(
                 gpu(0x4000U),
                 0x100U)
            .has_value());

    const astraea::graphics::GraphicsIrGpuMemoryWrite operation{
        .destination = gpu(0x4000U),
        .values = {},
    };

    const auto result =
        astraea::graphics::resolve_gpu_memory_write(
            operation,
            space);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GpuMemoryWriteResolutionErrorCode::
                address_resolution_failure);
    REQUIRE(
        result.error().address_resolution_error->code ==
        astraea::graphics::
            GuestGpuBufferResolutionErrorCode::
                zero_sized_request);
}
