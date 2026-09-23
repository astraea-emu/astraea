#include <astraea/graphics/vulkan_gpu_buffer_write_execution.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <volk.h>

namespace astraea::graphics {
namespace {

[[nodiscard]] VulkanGpuBufferWriteError make_error(
    VulkanGpuBufferWriteErrorCode code,
    VkResult result = VK_SUCCESS) noexcept {
    return VulkanGpuBufferWriteError{
        .code = code,
        .native_result =
            static_cast<std::int32_t>(result),
    };
}

[[nodiscard]] bool checked_end(
    std::uint64_t begin,
    std::uint64_t size,
    std::uint64_t& end) noexcept {
    if (begin >
        std::numeric_limits<std::uint64_t>::max() -
            size) {
        return false;
    }
    end = begin + size;
    return true;
}

struct VulkanResources {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VulkanResources() = default;
    VulkanResources(const VulkanResources&) = delete;
    VulkanResources& operator=(const VulkanResources&) = delete;

    ~VulkanResources() {
        if (device != VK_NULL_HANDLE) {
            if (mapped != nullptr &&
                memory != VK_NULL_HANDLE) {
                vkUnmapMemory(device, memory);
                mapped = nullptr;
            }
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, fence, nullptr);
            }
            if (command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(
                    device,
                    command_pool,
                    nullptr);
            }
            if (buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(
                    device,
                    buffer,
                    nullptr);
            }
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(
                    device,
                    memory,
                    nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

[[nodiscard]] bool has_extension(
    std::span<const VkExtensionProperties> properties,
    const char* name) noexcept {
    return std::any_of(
        properties.begin(),
        properties.end(),
        [name](const VkExtensionProperties& property) {
            return std::strcmp(
                       property.extensionName,
                       name) == 0;
        });
}

using ExtensionResult =
    astraea::core::Result<
        std::vector<VkExtensionProperties>,
        VulkanGpuBufferWriteError>;

[[nodiscard]] ExtensionResult
enumerate_instance_extensions() {
    std::uint32_t count = 0;
    auto result =
        vkEnumerateInstanceExtensionProperties(
            nullptr,
            &count,
            nullptr);
    if (result != VK_SUCCESS) {
        return ExtensionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    instance_creation_failure,
                result));
    }

    try {
        std::vector<VkExtensionProperties>
            properties(count);
        if (count != 0U) {
            result =
                vkEnumerateInstanceExtensionProperties(
                    nullptr,
                    &count,
                    properties.data());
            if (result != VK_SUCCESS) {
                return ExtensionResult::failure(
                    make_error(
                        VulkanGpuBufferWriteErrorCode::
                            instance_creation_failure,
                        result));
            }
            properties.resize(count);
        }
        return ExtensionResult::success(
            std::move(properties));
    } catch (const std::bad_alloc&) {
        return ExtensionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    host_allocation_failure));
    }
}

[[nodiscard]] ExtensionResult
enumerate_device_extensions(
    VkPhysicalDevice physical_device) {
    std::uint32_t count = 0;
    auto result =
        vkEnumerateDeviceExtensionProperties(
            physical_device,
            nullptr,
            &count,
            nullptr);
    if (result != VK_SUCCESS) {
        return ExtensionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    device_extension_enumeration_failure,
                result));
    }

    try {
        std::vector<VkExtensionProperties>
            properties(count);
        if (count != 0U) {
            result =
                vkEnumerateDeviceExtensionProperties(
                    physical_device,
                    nullptr,
                    &count,
                    properties.data());
            if (result != VK_SUCCESS) {
                return ExtensionResult::failure(
                    make_error(
                        VulkanGpuBufferWriteErrorCode::
                            device_extension_enumeration_failure,
                        result));
            }
            properties.resize(count);
        }
        return ExtensionResult::success(
            std::move(properties));
    } catch (const std::bad_alloc&) {
        return ExtensionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    host_allocation_failure));
    }
}

struct PhysicalDeviceSelection {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    std::uint32_t queue_family_index = 0;
    VkPhysicalDeviceProperties properties{};
};

using PhysicalDeviceSelectionResult =
    astraea::core::Result<
        PhysicalDeviceSelection,
        VulkanGpuBufferWriteError>;

[[nodiscard]] PhysicalDeviceSelectionResult
select_physical_device(VkInstance instance) {
    std::uint32_t device_count = 0;
    auto result =
        vkEnumeratePhysicalDevices(
            instance,
            &device_count,
            nullptr);
    if (result != VK_SUCCESS) {
        return PhysicalDeviceSelectionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    physical_device_enumeration_failure,
                result));
    }
    if (device_count == 0U) {
        return PhysicalDeviceSelectionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    no_physical_device));
    }

    try {
        std::vector<VkPhysicalDevice> devices(
            device_count);
        result =
            vkEnumeratePhysicalDevices(
                instance,
                &device_count,
                devices.data());
        if (result != VK_SUCCESS) {
            return PhysicalDeviceSelectionResult::failure(
                make_error(
                    VulkanGpuBufferWriteErrorCode::
                        physical_device_enumeration_failure,
                    result));
        }
        devices.resize(device_count);

        for (const auto physical_device : devices) {
            std::uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(
                physical_device,
                &queue_count,
                nullptr);
            if (queue_count == 0U) {
                continue;
            }

            std::vector<VkQueueFamilyProperties>
                queue_properties(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(
                physical_device,
                &queue_count,
                queue_properties.data());
            queue_properties.resize(queue_count);

            std::vector<VulkanGpuBufferWriteQueueCandidate>
                candidates;
            candidates.reserve(
                queue_properties.size());
            for (const auto& queue : queue_properties) {
                const auto supported_flags =
                    VK_QUEUE_TRANSFER_BIT |
                    VK_QUEUE_GRAPHICS_BIT |
                    VK_QUEUE_COMPUTE_BIT;
                candidates.push_back(
                    VulkanGpuBufferWriteQueueCandidate{
                        .supports_update_buffer =
                            (queue.queueFlags &
                             supported_flags) != 0U,
                        .queue_count = queue.queueCount,
                    });
            }

            const auto family =
                select_vulkan_gpu_buffer_write_queue_family(
                    candidates);
            if (!family.has_value()) {
                continue;
            }

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(
                physical_device,
                &properties);

            return PhysicalDeviceSelectionResult::success(
                PhysicalDeviceSelection{
                    .physical_device = physical_device,
                    .queue_family_index =
                        family.value(),
                    .properties = properties,
                });
        }
    } catch (const std::bad_alloc&) {
        return PhysicalDeviceSelectionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    host_allocation_failure));
    }

    return PhysicalDeviceSelectionResult::failure(
        make_error(
            VulkanGpuBufferWriteErrorCode::
                no_update_buffer_queue_family));
}

struct MemorySelection {
    std::uint32_t index = 0;
    bool coherent = false;
};

[[nodiscard]] std::optional<MemorySelection>
select_memory_type(
    std::uint32_t type_bits,
    const VkPhysicalDeviceMemoryProperties& properties) {
    std::array<
        VulkanGpuBufferWriteMemoryCandidate,
        VK_MAX_MEMORY_TYPES>
        candidates{};

    const auto count =
        std::min<std::uint32_t>(
            properties.memoryTypeCount,
            VK_MAX_MEMORY_TYPES);
    for (std::uint32_t index = 0;
         index < count;
         ++index) {
        const auto flags =
            properties.memoryTypes[index].
                propertyFlags;
        candidates[index] =
            VulkanGpuBufferWriteMemoryCandidate{
                .allowed =
                    (type_bits &
                     (std::uint32_t{1} << index)) !=
                    0U,
                .host_visible =
                    (flags &
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) !=
                    0U,
                .host_coherent =
                    (flags &
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) !=
                    0U,
            };
    }

    const auto selected =
        select_vulkan_gpu_buffer_write_memory_type(
            std::span{
                candidates.data(),
                static_cast<std::size_t>(count)});
    if (!selected.has_value()) {
        return std::nullopt;
    }

    const auto index = selected.value();
    return MemorySelection{
        .index = index,
        .coherent =
            candidates[index].host_coherent,
    };
}

}  // namespace

std::optional<std::uint32_t>
select_vulkan_gpu_buffer_write_queue_family(
    std::span<const VulkanGpuBufferWriteQueueCandidate>
        candidates) noexcept {
    for (std::size_t index = 0;
         index < candidates.size();
         ++index) {
        if (candidates[index].supports_update_buffer &&
            candidates[index].queue_count != 0U &&
            index <=
                std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
select_vulkan_gpu_buffer_write_memory_type(
    std::span<const VulkanGpuBufferWriteMemoryCandidate>
        candidates) noexcept {
    const auto choose =
        [candidates](bool require_coherent)
        -> std::optional<std::uint32_t> {
        for (std::size_t index = 0;
             index < candidates.size();
             ++index) {
            const auto& candidate =
                candidates[index];
            if (!candidate.allowed ||
                !candidate.host_visible ||
                (require_coherent &&
                 !candidate.host_coherent) ||
                index >
                    std::numeric_limits<
                        std::uint32_t>::max()) {
                continue;
            }
            return static_cast<std::uint32_t>(
                index);
        }
        return std::nullopt;
    };

    if (const auto coherent = choose(true);
        coherent.has_value()) {
        return coherent;
    }
    return choose(false);
}

VulkanGpuBufferWritePlanResult
plan_vulkan_gpu_buffer_write(
    const GuestGpuBufferRegion& region,
    const GuestGpuBufferResolution& resolution,
    const GraphicsIrGpuMemoryWrite& operation) noexcept {
    std::uint64_t region_end = 0;
    if (region.byte_size == 0U ||
        !checked_end(
            region.base.value,
            region.byte_size,
            region_end)) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    invalid_region));
    }

    if (resolution.buffer_id != region.id) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    buffer_id_mismatch));
    }

    if (resolution.byte_count == 0U) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    zero_sized_resolution));
    }

    if (resolution.byte_offset > region.byte_size ||
        resolution.byte_count >
            region.byte_size - resolution.byte_offset) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    resolution_out_of_bounds));
    }

    if (operation.values.empty()) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    empty_payload));
    }

    constexpr std::uint64_t kDwordBytes = 4U;
    const auto value_count =
        static_cast<std::uint64_t>(
            operation.values.size());
    if (value_count >
        std::numeric_limits<std::uint64_t>::max() /
            kDwordBytes) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    payload_size_overflow));
    }
    const auto payload_bytes =
        value_count * kDwordBytes;

    if (payload_bytes != resolution.byte_count) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    payload_size_mismatch));
    }

    std::uint64_t expected_destination = 0;
    if (!checked_end(
            region.base.value,
            resolution.byte_offset,
            expected_destination) ||
        expected_destination !=
            operation.destination.value) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    destination_mismatch));
    }

    if ((operation.destination.value & 0x3ULL) != 0U ||
        (resolution.byte_offset & 0x3ULL) != 0U ||
        (payload_bytes & 0x3ULL) != 0U) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    unaligned_write));
    }

    if (payload_bytes >
        kVulkanGpuBufferWriteMaxUpdateBytes) {
        return VulkanGpuBufferWritePlanResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    payload_too_large));
    }

    return VulkanGpuBufferWritePlanResult::success(
        VulkanGpuBufferWritePlan{
            .buffer_id = region.id,
            .buffer_byte_size = region.byte_size,
            .byte_offset = resolution.byte_offset,
            .byte_count = payload_bytes,
        });
}

VulkanGpuBufferWriteExecutionResult
execute_gpu_buffer_write_on_vulkan(
    const GuestGpuBufferRegion& region,
    const GuestGpuBufferResolution& resolution,
    const GraphicsIrGpuMemoryWrite& operation) {
    const auto plan =
        plan_vulkan_gpu_buffer_write(
            region,
            resolution,
            operation);
    if (!plan.has_value()) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            plan.error());
    }

    const auto buffer_size =
        static_cast<std::size_t>(
            plan->buffer_byte_size);
    const auto byte_offset =
        static_cast<VkDeviceSize>(
            plan->byte_offset);
    const auto byte_count =
        static_cast<VkDeviceSize>(
            plan->byte_count);

    auto result = volkInitialize();
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    loader_unavailable,
                result));
    }

    VulkanResources resources;

    auto instance_extensions =
        enumerate_instance_extensions();
    if (!instance_extensions.has_value()) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            instance_extensions.error());
    }

    std::vector<const char*>
        enabled_instance_extensions;
    VkInstanceCreateFlags instance_flags = 0U;
#ifdef VK_KHR_portability_enumeration
    if (has_extension(
            instance_extensions.value(),
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        enabled_instance_extensions.push_back(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instance_flags |=
            VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    const VkApplicationInfo application_info{
        .sType =
            VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Astraea",
        .applicationVersion = 0U,
        .pEngineName = "Astraea",
        .engineVersion = 0U,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo instance_info{
        .sType =
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = instance_flags,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount =
            static_cast<std::uint32_t>(
                enabled_instance_extensions.size()),
        .ppEnabledExtensionNames =
            enabled_instance_extensions.empty()
                ? nullptr
                : enabled_instance_extensions.data(),
    };

    result =
        vkCreateInstance(
            &instance_info,
            nullptr,
            &resources.instance);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    instance_creation_failure,
                result));
    }
    volkLoadInstance(resources.instance);

    auto selected =
        select_physical_device(resources.instance);
    if (!selected.has_value()) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            selected.error());
    }
    const auto physical = selected.value();

    auto device_extensions =
        enumerate_device_extensions(
            physical.physical_device);
    if (!device_extensions.has_value()) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            device_extensions.error());
    }

    std::vector<const char*>
        enabled_device_extensions;
#ifdef VK_KHR_portability_subset
    if (has_extension(
            device_extensions.value(),
            VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        enabled_device_extensions.push_back(
            VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
#endif

    constexpr float queue_priority = 1.0F;
    const VkDeviceQueueCreateInfo queue_info{
        .sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueFamilyIndex =
            physical.queue_family_index,
        .queueCount = 1U,
        .pQueuePriorities = &queue_priority,
    };
    const VkDeviceCreateInfo device_info{
        .sType =
            VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueCreateInfoCount = 1U,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount =
            static_cast<std::uint32_t>(
                enabled_device_extensions.size()),
        .ppEnabledExtensionNames =
            enabled_device_extensions.empty()
                ? nullptr
                : enabled_device_extensions.data(),
        .pEnabledFeatures = nullptr,
    };

    result =
        vkCreateDevice(
            physical.physical_device,
            &device_info,
            nullptr,
            &resources.device);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    device_creation_failure,
                result));
    }
    volkLoadDevice(resources.device);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(
        resources.device,
        physical.queue_family_index,
        0U,
        &queue);

    const VkBufferCreateInfo buffer_info{
        .sType =
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .size =
            static_cast<VkDeviceSize>(
                plan->buffer_byte_size),
        .usage =
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0U,
        .pQueueFamilyIndices = nullptr,
    };
    result =
        vkCreateBuffer(
            resources.device,
            &buffer_info,
            nullptr,
            &resources.buffer);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    buffer_creation_failure,
                result));
    }

    VkMemoryRequirements memory_requirements{};
    vkGetBufferMemoryRequirements(
        resources.device,
        resources.buffer,
        &memory_requirements);

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(
        physical.physical_device,
        &memory_properties);
    const auto memory_selection =
        select_memory_type(
            memory_requirements.memoryTypeBits,
            memory_properties);
    if (!memory_selection.has_value()) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    no_host_visible_memory_type));
    }

    const VkMemoryAllocateInfo allocation_info{
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize =
            memory_requirements.size,
        .memoryTypeIndex =
            memory_selection->index,
    };
    result =
        vkAllocateMemory(
            resources.device,
            &allocation_info,
            nullptr,
            &resources.memory);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    memory_allocation_failure,
                result));
    }

    result =
        vkBindBufferMemory(
            resources.device,
            resources.buffer,
            resources.memory,
            0U);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    memory_bind_failure,
                result));
    }

    result =
        vkMapMemory(
            resources.device,
            resources.memory,
            0U,
            memory_requirements.size,
            0U,
            &resources.mapped);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    memory_map_failure,
                result));
    }

    std::memset(
        resources.mapped,
        0,
        buffer_size);

    if (!memory_selection->coherent) {
        const VkMappedMemoryRange range{
            .sType =
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = resources.memory,
            .offset = 0U,
            .size = VK_WHOLE_SIZE,
        };
        result =
            vkFlushMappedMemoryRanges(
                resources.device,
                1U,
                &range);
        if (result != VK_SUCCESS) {
            return VulkanGpuBufferWriteExecutionResult::failure(
                make_error(
                    VulkanGpuBufferWriteErrorCode::
                        memory_flush_failure,
                    result));
        }
    }

    const VkCommandPoolCreateInfo command_pool_info{
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex =
            physical.queue_family_index,
    };
    result =
        vkCreateCommandPool(
            resources.device,
            &command_pool_info,
            nullptr,
            &resources.command_pool);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    command_pool_creation_failure,
                result));
    }

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo command_allocate_info{
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = resources.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1U,
    };
    result =
        vkAllocateCommandBuffers(
            resources.device,
            &command_allocate_info,
            &command_buffer);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    command_buffer_allocation_failure,
                result));
    }

    const VkCommandBufferBeginInfo command_begin_info{
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags =
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    result =
        vkBeginCommandBuffer(
            command_buffer,
            &command_begin_info);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    command_buffer_begin_failure,
                result));
    }

    vkCmdUpdateBuffer(
        command_buffer,
        resources.buffer,
        byte_offset,
        byte_count,
        operation.values.data());

    const VkBufferMemoryBarrier barrier{
        .sType =
            VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .buffer = resources.buffer,
        .offset = byte_offset,
        .size = byte_count,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0U,
        0U,
        nullptr,
        1U,
        &barrier,
        0U,
        nullptr);

    result =
        vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    command_buffer_end_failure,
                result));
    }

    const VkFenceCreateInfo fence_info{
        .sType =
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
    };
    result =
        vkCreateFence(
            resources.device,
            &fence_info,
            nullptr,
            &resources.fence);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    fence_creation_failure,
                result));
    }

    const VkSubmitInfo submit_info{
        .sType =
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0U,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1U,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0U,
        .pSignalSemaphores = nullptr,
    };
    result =
        vkQueueSubmit(
            queue,
            1U,
            &submit_info,
            resources.fence);
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    queue_submit_failure,
                result));
    }

    result =
        vkWaitForFences(
            resources.device,
            1U,
            &resources.fence,
            VK_TRUE,
            std::numeric_limits<std::uint64_t>::max());
    if (result != VK_SUCCESS) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    fence_wait_failure,
                result));
    }

    if (!memory_selection->coherent) {
        const VkMappedMemoryRange range{
            .sType =
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = resources.memory,
            .offset = 0U,
            .size = VK_WHOLE_SIZE,
        };
        result =
            vkInvalidateMappedMemoryRanges(
                resources.device,
                1U,
                &range);
        if (result != VK_SUCCESS) {
            return VulkanGpuBufferWriteExecutionResult::failure(
                make_error(
                    VulkanGpuBufferWriteErrorCode::
                        memory_invalidate_failure,
                    result));
        }
    }

    try {
        std::vector<std::byte> final_bytes(
            buffer_size);
        std::memcpy(
            final_bytes.data(),
            resources.mapped,
            buffer_size);

        return VulkanGpuBufferWriteExecutionResult::success(
            VulkanGpuBufferWriteExecution{
                .buffer_id = plan->buffer_id,
                .final_bytes =
                    std::move(final_bytes),
                .device =
                    VulkanGpuBufferWriteDeviceInfo{
                        .name =
                            physical.properties.deviceName,
                        .api_version =
                            physical.properties.apiVersion,
                        .vendor_id =
                            physical.properties.vendorID,
                        .device_id =
                            physical.properties.deviceID,
                        .device_type =
                            static_cast<std::uint32_t>(
                                physical.properties.deviceType),
                    },
            });
    } catch (const std::bad_alloc&) {
        return VulkanGpuBufferWriteExecutionResult::failure(
            make_error(
                VulkanGpuBufferWriteErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::graphics
