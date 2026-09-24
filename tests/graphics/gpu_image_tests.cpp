#include <astraea/graphics/gpu_image.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::GpuVirtualAddress gpu(
    std::uint64_t value) {
    return astraea::graphics::GpuVirtualAddress{
        .value = value,
    };
}

astraea::graphics::GuestGpuImageDescriptor
valid_descriptor(
    std::uint64_t base = 0x1200U) {
    return astraea::graphics::GuestGpuImageDescriptor{
        .base_address = gpu(base),
        .format =
            astraea::graphics::GuestGpuImageFormat::
                r8g8b8a8_unorm,
        .width = 4U,
        .height = 4U,
        .bytes_per_pixel = 4U,
        .logical_byte_count = 64U,
        .layout =
            astraea::graphics::GuestGpuSurfaceLayout{
                .kind =
                    astraea::graphics::
                        GuestGpuSurfaceLayoutKind::
                            gfx10_aligned_linear,
                .pitch_pixels = 64U,
                .pitch_bytes = 256U,
                .base_alignment_bytes = 256U,
                .surface_byte_count = 1024U,
            },
    };
}

}  // namespace

TEST_CASE(
    "guest GPU image view resolves exact backing allocation and offset",
    "[graphics][gpu-image][registration]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;

    const auto allocation =
        allocations.register_allocation(
            gpu(0x1000U),
            0x2000U);
    REQUIRE(allocation.has_value());

    const auto descriptor = valid_descriptor();
    const auto image =
        images.register_view(descriptor, allocations);

    REQUIRE(image.has_value());
    REQUIRE(image->value == 0U);

    const auto* view = images.entry_at(image.value());
    REQUIRE(view != nullptr);
    REQUIRE(view->allocation_id == allocation.value());
    REQUIRE(view->allocation_byte_offset == 0x200U);
    REQUIRE(view->descriptor.logical_byte_count == 64U);
    REQUIRE(
        view->descriptor.layout.surface_byte_count ==
        1024U);
}

TEST_CASE(
    "guest GPU image view requires complete physical surface backing",
    "[graphics][gpu-image][registration][negative]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;

    REQUIRE(
        allocations.register_allocation(
                       gpu(0x1200U),
                       64U)
            .has_value());

    const auto result =
        images.register_view(
            valid_descriptor(),
            allocations);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GuestGpuImageRegistrationErrorCode::
                allocation_resolution_failure);
    REQUIRE(
        result.error().allocation_resolution_error.has_value());
    REQUIRE(
        result.error().allocation_resolution_error->code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                partially_mapped_range);
}

TEST_CASE(
    "guest GPU image validation keeps logical and physical layout consistent",
    "[graphics][gpu-image][registration][negative][layout]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;
    REQUIRE(
        allocations.register_allocation(
                       gpu(0x1000U),
                       0x2000U)
            .has_value());

    SECTION("logical size") {
        auto descriptor = valid_descriptor();
        descriptor.logical_byte_count = 1024U;

        const auto result =
            images.register_view(
                descriptor,
                allocations);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuImageRegistrationErrorCode::
                    logical_size_mismatch);
    }

    SECTION("pitch bytes") {
        auto descriptor = valid_descriptor();
        descriptor.layout.pitch_bytes = 16U;

        const auto result =
            images.register_view(
                descriptor,
                allocations);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuImageRegistrationErrorCode::
                    pitch_size_mismatch);
    }

    SECTION("surface extent") {
        auto descriptor = valid_descriptor();
        descriptor.layout.surface_byte_count = 64U;

        const auto result =
            images.register_view(
                descriptor,
                allocations);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuImageRegistrationErrorCode::
                    surface_size_mismatch);
    }

    SECTION("alignment") {
        auto descriptor =
            valid_descriptor(0x1204U);

        const auto result =
            images.register_view(
                descriptor,
                allocations);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuImageRegistrationErrorCode::
                    misaligned_base_address);
    }
}

TEST_CASE(
    "guest GPU image registry preserves alias views and reports exact ambiguity",
    "[graphics][gpu-image][resolve]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;
    REQUIRE(
        allocations.register_allocation(
                       gpu(0x1000U),
                       0x2000U)
            .has_value());

    const auto descriptor = valid_descriptor();

    const auto first =
        images.register_view(
            descriptor,
            allocations);
    REQUIRE(first.has_value());

    const auto unique =
        images.resolve_exact(descriptor);
    REQUIRE(unique.has_value());
    REQUIRE(unique->id == first.value());

    const auto second =
        images.register_view(
            descriptor,
            allocations);
    REQUIRE(second.has_value());
    REQUIRE(second->value == 1U);

    const auto ambiguous =
        images.resolve_exact(descriptor);
    REQUIRE_FALSE(ambiguous.has_value());
    REQUIRE(
        ambiguous.error().code ==
        astraea::graphics::
            GuestGpuImageResolutionErrorCode::
                ambiguous);
    REQUIRE(ambiguous.error().match_count == 2U);
    REQUIRE(
        ambiguous.error().first_matching_image_id ==
        first.value());

    auto other = descriptor;
    other.width = 8U;
    const auto missing =
        images.resolve_exact(other);
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(
        missing.error().code ==
        astraea::graphics::
            GuestGpuImageResolutionErrorCode::
                not_found);
}

TEST_CASE(
    "guest GPU image validation rejects zero dimensions and impossible pitch",
    "[graphics][gpu-image][registration][negative][shape]") {
    astraea::graphics::GuestGpuAllocationAddressSpace allocations;
    astraea::graphics::GuestGpuImageRegistry images;
    REQUIRE(
        allocations.register_allocation(
                       gpu(0x1000U),
                       0x2000U)
            .has_value());

    SECTION("zero dimension") {
        auto descriptor = valid_descriptor();
        descriptor.width = 0U;
        const auto result =
            images.register_view(descriptor, allocations);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuImageRegistrationErrorCode::
                    zero_dimension);
    }

    SECTION("pitch smaller than width") {
        auto descriptor = valid_descriptor();
        descriptor.layout.pitch_pixels = 3U;
        descriptor.layout.pitch_bytes = 12U;
        const auto result =
            images.register_view(descriptor, allocations);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GuestGpuImageRegistrationErrorCode::
                    pitch_smaller_than_width);
    }
}
