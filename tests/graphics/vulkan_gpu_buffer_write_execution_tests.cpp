#include <astraea/graphics/vulkan_gpu_buffer_write_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::graphics::GraphicsIrGpuMemoryWrite;
using astraea::graphics::GuestGpuBufferId;
using astraea::graphics::GuestGpuBufferRegion;
using astraea::graphics::GuestGpuBufferResolution;
using astraea::graphics::GpuVirtualAddress;
using astraea::graphics::VulkanGpuBufferWriteErrorCode;
using astraea::graphics::VulkanGpuBufferWriteMemoryCandidate;
using astraea::graphics::VulkanGpuBufferWriteQueueCandidate;

[[nodiscard]] GuestGpuBufferRegion region(
    std::uint64_t base = 0x4000U,
    std::uint64_t byte_size = 64U,
    std::uint64_t id = 7U) {
    return GuestGpuBufferRegion{
        .id = GuestGpuBufferId{.value = id},
        .base = GpuVirtualAddress{.value = base},
        .byte_size = byte_size,
    };
}

[[nodiscard]] GuestGpuBufferResolution resolution(
    std::uint64_t byte_offset = 8U,
    std::uint64_t byte_count = 16U,
    std::uint64_t id = 7U) {
    return GuestGpuBufferResolution{
        .buffer_id = GuestGpuBufferId{.value = id},
        .byte_offset = byte_offset,
        .byte_count = byte_count,
    };
}

[[nodiscard]] GraphicsIrGpuMemoryWrite operation(
    std::uint64_t destination = 0x4008U,
    std::vector<std::uint32_t> values =
        std::vector<std::uint32_t>{
            0x11223344U,
            0x55667788U,
            0x99aabbccU,
            0xddeeff00U,
        }) {
    return GraphicsIrGpuMemoryWrite{
        .destination =
            GpuVirtualAddress{
                .value = destination,
            },
        .values = std::move(values),
    };
}

[[nodiscard]] bool live_vulkan_required() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(
            &value,
            &value_size,
            "ASTRAEA_REQUIRE_VULKAN_PROBE") != 0 ||
        value == nullptr) {
        return false;
    }

    const bool required =
        std::string_view{value} == "1";
    std::free(value);
    return required;
#else
    const auto* value =
        std::getenv("ASTRAEA_REQUIRE_VULKAN_PROBE");
    return value != nullptr &&
           std::string_view{value} == "1";
#endif
}

}  // namespace

TEST_CASE(
    "Vulkan buffer-write planner accepts consistent W0 W1 state",
    "[graphics][vulkan][write-data][plan]") {
    const auto planned =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(),
                operation());

    REQUIRE(planned.has_value());
    REQUIRE(planned->buffer_id.value == 7U);
    REQUIRE(planned->buffer_byte_size == 64U);
    REQUIRE(planned->byte_offset == 8U);
    REQUIRE(planned->byte_count == 16U);
}

TEST_CASE(
    "Vulkan buffer-write planner rejects invalid guest region",
    "[graphics][vulkan][write-data][plan][negative]") {
    const auto zero =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(0x4000U, 0U),
                resolution(),
                operation());
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        VulkanGpuBufferWriteErrorCode::
            invalid_region);

    const auto overflow =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(
                    std::numeric_limits<std::uint64_t>::max() -
                        3U,
                    4U),
                resolution(),
                operation());
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(
        overflow.error().code ==
        VulkanGpuBufferWriteErrorCode::
            invalid_region);
}

TEST_CASE(
    "Vulkan buffer-write planner rejects mismatched guest buffer identity",
    "[graphics][vulkan][write-data][plan][negative]") {
    const auto planned =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(8U, 16U, 8U),
                operation());

    REQUIRE_FALSE(planned.has_value());
    REQUIRE(
        planned.error().code ==
        VulkanGpuBufferWriteErrorCode::
            buffer_id_mismatch);
}

TEST_CASE(
    "Vulkan buffer-write planner rejects zero or out-of-bounds resolution",
    "[graphics][vulkan][write-data][plan][negative]") {
    const auto zero =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(8U, 0U),
                operation());
    REQUIRE_FALSE(zero.has_value());
    REQUIRE(
        zero.error().code ==
        VulkanGpuBufferWriteErrorCode::
            zero_sized_resolution);

    const auto outside =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(60U, 16U),
                operation(0x403cU));
    REQUIRE_FALSE(outside.has_value());
    REQUIRE(
        outside.error().code ==
        VulkanGpuBufferWriteErrorCode::
            resolution_out_of_bounds);
}

TEST_CASE(
    "Vulkan buffer-write planner rejects empty or mismatched payload",
    "[graphics][vulkan][write-data][plan][negative]") {
    const auto empty =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(),
                operation(
                    0x4008U,
                    {}));
    REQUIRE_FALSE(empty.has_value());
    REQUIRE(
        empty.error().code ==
        VulkanGpuBufferWriteErrorCode::
            empty_payload);

    const auto mismatch =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(),
                operation(
                    0x4008U,
                    {0x11111111U}));
    REQUIRE_FALSE(mismatch.has_value());
    REQUIRE(
        mismatch.error().code ==
        VulkanGpuBufferWriteErrorCode::
            payload_size_mismatch);
}

TEST_CASE(
    "Vulkan buffer-write planner rejects destination mismatch",
    "[graphics][vulkan][write-data][plan][negative]") {
    const auto planned =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(),
                operation(0x4010U));

    REQUIRE_FALSE(planned.has_value());
    REQUIRE(
        planned.error().code ==
        VulkanGpuBufferWriteErrorCode::
            destination_mismatch);
}

TEST_CASE(
    "Vulkan buffer-write planner rejects unaligned resolved write",
    "[graphics][vulkan][write-data][plan][negative]") {
    const auto planned =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(),
                resolution(2U, 16U),
                operation(0x4002U));

    REQUIRE_FALSE(planned.has_value());
    REQUIRE(
        planned.error().code ==
        VulkanGpuBufferWriteErrorCode::
            unaligned_write);
}

TEST_CASE(
    "Vulkan buffer-write planner enforces update-buffer size ceiling",
    "[graphics][vulkan][write-data][plan][boundary]") {
    constexpr std::size_t max_w0_dwords = 16381U;
    constexpr std::size_t too_many_dwords = 16385U;

    const auto max_values =
        std::vector<std::uint32_t>(
            max_w0_dwords,
            0x12345678U);
    const auto max_bytes =
        static_cast<std::uint64_t>(
            max_values.size()) *
        4U;
    const auto max_plan =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(
                    0x100000U,
                    max_bytes,
                    1U),
                resolution(
                    0U,
                    max_bytes,
                    1U),
                operation(
                    0x100000U,
                    max_values));

    REQUIRE(max_plan.has_value());
    REQUIRE(max_plan->byte_count == 65524U);

    const auto too_large_values =
        std::vector<std::uint32_t>(
            too_many_dwords,
            0x12345678U);
    const auto too_large_bytes =
        static_cast<std::uint64_t>(
            too_large_values.size()) *
        4U;
    const auto too_large =
        astraea::graphics::
            plan_vulkan_gpu_buffer_write(
                region(
                    0x200000U,
                    too_large_bytes,
                    2U),
                resolution(
                    0U,
                    too_large_bytes,
                    2U),
                operation(
                    0x200000U,
                    too_large_values));

    REQUIRE_FALSE(too_large.has_value());
    REQUIRE(
        too_large.error().code ==
        VulkanGpuBufferWriteErrorCode::
            payload_too_large);
}

TEST_CASE(
    "Vulkan buffer-write queue selection chooses first usable family",
    "[graphics][vulkan][write-data][selection]") {
    const std::array<VulkanGpuBufferWriteQueueCandidate, 4>
        candidates{
            VulkanGpuBufferWriteQueueCandidate{
                .supports_update_buffer = false,
                .queue_count = 2U,
            },
            VulkanGpuBufferWriteQueueCandidate{
                .supports_update_buffer = true,
                .queue_count = 0U,
            },
            VulkanGpuBufferWriteQueueCandidate{
                .supports_update_buffer = true,
                .queue_count = 1U,
            },
            VulkanGpuBufferWriteQueueCandidate{
                .supports_update_buffer = true,
                .queue_count = 4U,
            },
        };

    REQUIRE(
        astraea::graphics::
            select_vulkan_gpu_buffer_write_queue_family(
                candidates) ==
        std::optional<std::uint32_t>{2U});
}

TEST_CASE(
    "Vulkan buffer-write queue selection rejects no usable family",
    "[graphics][vulkan][write-data][selection]") {
    const std::array<VulkanGpuBufferWriteQueueCandidate, 2>
        candidates{
            VulkanGpuBufferWriteQueueCandidate{
                .supports_update_buffer = false,
                .queue_count = 1U,
            },
            VulkanGpuBufferWriteQueueCandidate{
                .supports_update_buffer = true,
                .queue_count = 0U,
            },
        };

    REQUIRE_FALSE(
        astraea::graphics::
            select_vulkan_gpu_buffer_write_queue_family(
                candidates)
            .has_value());
}

TEST_CASE(
    "Vulkan buffer-write memory selection prefers coherent host-visible memory",
    "[graphics][vulkan][write-data][selection]") {
    const std::array<VulkanGpuBufferWriteMemoryCandidate, 4>
        candidates{
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = true,
                .host_visible = false,
                .host_coherent = false,
            },
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = true,
                .host_visible = true,
                .host_coherent = false,
            },
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = false,
                .host_visible = true,
                .host_coherent = true,
            },
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = true,
                .host_visible = true,
                .host_coherent = true,
            },
        };

    REQUIRE(
        astraea::graphics::
            select_vulkan_gpu_buffer_write_memory_type(
                candidates) ==
        std::optional<std::uint32_t>{3U});
}

TEST_CASE(
    "Vulkan buffer-write memory selection falls back to noncoherent host-visible memory",
    "[graphics][vulkan][write-data][selection]") {
    const std::array<VulkanGpuBufferWriteMemoryCandidate, 3>
        candidates{
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = false,
                .host_visible = true,
                .host_coherent = true,
            },
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = true,
                .host_visible = true,
                .host_coherent = false,
            },
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed = true,
                .host_visible = false,
                .host_coherent = false,
            },
        };

    REQUIRE(
        astraea::graphics::
            select_vulkan_gpu_buffer_write_memory_type(
                candidates) ==
        std::optional<std::uint32_t>{1U});
}

TEST_CASE(
    "Vulkan buffer-write invalid input fails before loader use",
    "[graphics][vulkan][write-data][validation]") {
    const auto result =
        astraea::graphics::
            execute_gpu_buffer_write_on_vulkan(
                region(),
                resolution(8U, 16U, 99U),
                operation());

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        VulkanGpuBufferWriteErrorCode::
            buffer_id_mismatch);
}

TEST_CASE(
    "Vulkan buffer-write proof returns exact full-buffer bytes",
    "[graphics][vulkan][write-data][live][oracle]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    const auto guest_region = region();
    const auto resolved = resolution();
    const auto write = operation();

    const auto executed =
        astraea::graphics::
            execute_gpu_buffer_write_on_vulkan(
                guest_region,
                resolved,
                write);
    REQUIRE(executed.has_value());
    REQUIRE(executed->buffer_id == guest_region.id);
    REQUIRE(
        executed->final_bytes.size() ==
        static_cast<std::size_t>(
            guest_region.byte_size));
    REQUIRE_FALSE(executed->device.name.empty());

    std::vector<std::byte> expected(
        static_cast<std::size_t>(
            guest_region.byte_size),
        std::byte{0});
    std::memcpy(
        expected.data() +
            static_cast<std::size_t>(
                resolved.byte_offset),
        write.values.data(),
        static_cast<std::size_t>(
            resolved.byte_count));

    REQUIRE(executed->final_bytes == expected);
}
