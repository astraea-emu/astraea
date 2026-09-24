#include <astraea/graphics/vulkan_offscreen_image_execution.hpp>

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

[[nodiscard]] VulkanOffscreenImageError error(
    VulkanOffscreenImageErrorCode code,
    VkResult result = VK_SUCCESS) noexcept {
    return VulkanOffscreenImageError{
        .code = code,
        .native_result =
            static_cast<std::int32_t>(result),
    };
}

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
        VulkanOffscreenImageError>;

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
            error(
                VulkanOffscreenImageErrorCode::
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
                    error(
                        VulkanOffscreenImageErrorCode::
                            instance_creation_failure,
                        result));
            }
            properties.resize(count);
        }
        return ExtensionResult::success(
            std::move(properties));
    } catch (const std::bad_alloc&) {
        return ExtensionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
            error(
                VulkanOffscreenImageErrorCode::
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
                    error(
                        VulkanOffscreenImageErrorCode::
                            device_extension_enumeration_failure,
                        result));
            }
            properties.resize(count);
        }
        return ExtensionResult::success(
            std::move(properties));
    } catch (const std::bad_alloc&) {
        return ExtensionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
        VulkanOffscreenImageError>;

[[nodiscard]] PhysicalDeviceSelectionResult
select_physical_device(
    VkInstance instance,
    std::uint32_t width,
    std::uint32_t height) {
    std::uint32_t device_count = 0;
    auto result =
        vkEnumeratePhysicalDevices(
            instance,
            &device_count,
            nullptr);
    if (result != VK_SUCCESS) {
        return PhysicalDeviceSelectionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    physical_device_enumeration_failure,
                result));
    }
    if (device_count == 0U) {
        return PhysicalDeviceSelectionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
                error(
                    VulkanOffscreenImageErrorCode::
                        physical_device_enumeration_failure,
                    result));
        }
        devices.resize(device_count);

        constexpr VkImageUsageFlags usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        for (const auto physical_device : devices) {
            VkImageFormatProperties image_properties{};
            const auto image_result =
                vkGetPhysicalDeviceImageFormatProperties(
                    physical_device,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL,
                    usage,
                    0U,
                    &image_properties);
            if (image_result != VK_SUCCESS ||
                width > image_properties.maxExtent.width ||
                height > image_properties.maxExtent.height) {
                continue;
            }

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

            std::vector<VulkanOffscreenImageQueueCandidate>
                candidates;
            candidates.reserve(
                queue_properties.size());
            for (const auto& queue : queue_properties) {
                candidates.push_back(
                    VulkanOffscreenImageQueueCandidate{
                        .supports_graphics =
                            (queue.queueFlags &
                             VK_QUEUE_GRAPHICS_BIT) != 0U,
                        .queue_count = queue.queueCount,
                    });
            }

            const auto family =
                select_vulkan_offscreen_graphics_queue_family(
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
            error(
                VulkanOffscreenImageErrorCode::
                    host_allocation_failure));
    }

    return PhysicalDeviceSelectionResult::failure(
        error(
            VulkanOffscreenImageErrorCode::
                no_compatible_graphics_device));
}

[[nodiscard]] std::array<
    VulkanOffscreenImageMemoryCandidate,
    VK_MAX_MEMORY_TYPES>
memory_candidates(
    std::uint32_t type_bits,
    const VkPhysicalDeviceMemoryProperties& properties) noexcept {
    std::array<
        VulkanOffscreenImageMemoryCandidate,
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
            VulkanOffscreenImageMemoryCandidate{
                .allowed =
                    (type_bits &
                     (std::uint32_t{1} << index)) != 0U,
                .device_local =
                    (flags &
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) !=
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

    return candidates;
}

struct VulkanResources {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void* staging_mapped = nullptr;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    VulkanResources() = default;
    VulkanResources(const VulkanResources&) = delete;
    VulkanResources& operator=(const VulkanResources&) = delete;

    ~VulkanResources() {
        if (device != VK_NULL_HANDLE) {
            if (staging_mapped != nullptr &&
                staging_memory != VK_NULL_HANDLE) {
                vkUnmapMemory(
                    device,
                    staging_memory);
                staging_mapped = nullptr;
            }
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(
                    device,
                    fence,
                    nullptr);
            }
            if (command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(
                    device,
                    command_pool,
                    nullptr);
            }
            if (image_view != VK_NULL_HANDLE) {
                vkDestroyImageView(
                    device,
                    image_view,
                    nullptr);
            }
            if (image != VK_NULL_HANDLE) {
                vkDestroyImage(
                    device,
                    image,
                    nullptr);
            }
            if (staging_buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(
                    device,
                    staging_buffer,
                    nullptr);
            }
            if (image_memory != VK_NULL_HANDLE) {
                vkFreeMemory(
                    device,
                    image_memory,
                    nullptr);
            }
            if (staging_memory != VK_NULL_HANDLE) {
                vkFreeMemory(
                    device,
                    staging_memory,
                    nullptr);
            }
            vkDestroyDevice(
                device,
                nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(
                instance,
                nullptr);
        }
    }
};

}  // namespace

std::optional<std::uint32_t>
select_vulkan_offscreen_graphics_queue_family(
    std::span<const VulkanOffscreenImageQueueCandidate>
        candidates) noexcept {
    for (std::size_t index = 0;
         index < candidates.size();
         ++index) {
        if (candidates[index].supports_graphics &&
            candidates[index].queue_count != 0U &&
            index <=
                std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
select_vulkan_offscreen_device_memory_type(
    std::span<const VulkanOffscreenImageMemoryCandidate>
        candidates) noexcept {
    const auto choose =
        [candidates](bool require_device_local)
        -> std::optional<std::uint32_t> {
        for (std::size_t index = 0;
             index < candidates.size();
             ++index) {
            const auto& candidate =
                candidates[index];
            if (!candidate.allowed ||
                (require_device_local &&
                 !candidate.device_local) ||
                index >
                    std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }
            return static_cast<std::uint32_t>(
                index);
        }
        return std::nullopt;
    };

    if (const auto local = choose(true);
        local.has_value()) {
        return local;
    }
    return choose(false);
}

std::optional<std::uint32_t>
select_vulkan_offscreen_readback_memory_type(
    std::span<const VulkanOffscreenImageMemoryCandidate>
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
                    std::numeric_limits<std::uint32_t>::max()) {
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

VulkanOffscreenImagePlanResult
plan_vulkan_offscreen_image(
    const GuestGpuImageView& image) noexcept {
    const auto& descriptor = image.descriptor;

    if (descriptor.format !=
        GuestGpuImageFormat::r8g8b8a8_unorm) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    unsupported_image_format));
    }

    if (descriptor.width == 0U ||
        descriptor.height == 0U) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    invalid_image_extent));
    }

    if (descriptor.bytes_per_pixel != 4U) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    invalid_bytes_per_pixel));
    }

    const auto width =
        static_cast<std::uint64_t>(
            descriptor.width);
    const auto height =
        static_cast<std::uint64_t>(
            descriptor.height);

    if (width >
        std::numeric_limits<std::uint64_t>::max() /
            height) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    logical_size_overflow));
    }
    const auto pixel_count = width * height;

    if (pixel_count >
        std::numeric_limits<std::uint64_t>::max() /
            descriptor.bytes_per_pixel) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    logical_size_overflow));
    }
    const auto expected_bytes =
        pixel_count *
        descriptor.bytes_per_pixel;

    if (descriptor.logical_byte_count !=
        expected_bytes) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    logical_size_mismatch));
    }

    if (expected_bytes >
            std::numeric_limits<std::size_t>::max() ||
        expected_bytes >
            std::numeric_limits<VkDeviceSize>::max()) {
        return VulkanOffscreenImagePlanResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    logical_size_unrepresentable));
    }

    return VulkanOffscreenImagePlanResult::success(
        VulkanOffscreenImagePlan{
            .image_id = image.id,
            .width = descriptor.width,
            .height = descriptor.height,
            .logical_byte_count = expected_bytes,
        });
}

VulkanOffscreenImageExecutionResult
execute_vulkan_offscreen_image_clear(
    const GuestGpuImageView& image,
    std::array<std::uint8_t, 4> rgba) {
    const auto plan =
        plan_vulkan_offscreen_image(image);
    if (!plan.has_value()) {
        return VulkanOffscreenImageExecutionResult::failure(
            plan.error());
    }

    auto result = volkInitialize();
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    loader_unavailable,
                result));
    }

    VulkanResources resources;

    auto instance_extensions =
        enumerate_instance_extensions();
    if (!instance_extensions.has_value()) {
        return VulkanOffscreenImageExecutionResult::failure(
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    instance_creation_failure,
                result));
    }
    volkLoadInstance(resources.instance);

    auto selected =
        select_physical_device(
            resources.instance,
            plan->width,
            plan->height);
    if (!selected.has_value()) {
        return VulkanOffscreenImageExecutionResult::failure(
            selected.error());
    }
    const auto physical = selected.value();

    auto device_extensions =
        enumerate_device_extensions(
            physical.physical_device);
    if (!device_extensions.has_value()) {
        return VulkanOffscreenImageExecutionResult::failure(
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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

    const VkImageCreateInfo image_info{
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent =
            VkExtent3D{
                .width = plan->width,
                .height = plan->height,
                .depth = 1U,
            },
        .mipLevels = 1U,
        .arrayLayers = 1U,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0U,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    result =
        vkCreateImage(
            resources.device,
            &image_info,
            nullptr,
            &resources.image);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    image_creation_failure,
                result));
    }

    VkMemoryRequirements image_requirements{};
    vkGetImageMemoryRequirements(
        resources.device,
        resources.image,
        &image_requirements);

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(
        physical.physical_device,
        &memory_properties);

    const auto image_candidates =
        memory_candidates(
            image_requirements.memoryTypeBits,
            memory_properties);
    const auto image_memory_type =
        select_vulkan_offscreen_device_memory_type(
            std::span{
                image_candidates.data(),
                static_cast<std::size_t>(
                    memory_properties.memoryTypeCount)});
    if (!image_memory_type.has_value()) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    no_device_memory_type));
    }

    const VkMemoryAllocateInfo image_allocation_info{
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex =
            image_memory_type.value(),
    };
    result =
        vkAllocateMemory(
            resources.device,
            &image_allocation_info,
            nullptr,
            &resources.image_memory);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    memory_allocation_failure,
                result));
    }

    result =
        vkBindImageMemory(
            resources.device,
            resources.image,
            resources.image_memory,
            0U);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    image_memory_bind_failure,
                result));
    }

    const VkImageViewCreateInfo image_view_info{
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .image = resources.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components =
            VkComponentMapping{
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            VkImageSubresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0U,
                .levelCount = 1U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
    };
    result =
        vkCreateImageView(
            resources.device,
            &image_view_info,
            nullptr,
            &resources.image_view);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    image_view_creation_failure,
                result));
    }

    const auto readback_size =
        static_cast<VkDeviceSize>(
            plan->logical_byte_count);
    const VkBufferCreateInfo buffer_info{
        .sType =
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .size = readback_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0U,
        .pQueueFamilyIndices = nullptr,
    };
    result =
        vkCreateBuffer(
            resources.device,
            &buffer_info,
            nullptr,
            &resources.staging_buffer);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    staging_buffer_creation_failure,
                result));
    }

    VkMemoryRequirements buffer_requirements{};
    vkGetBufferMemoryRequirements(
        resources.device,
        resources.staging_buffer,
        &buffer_requirements);

    const auto readback_candidates =
        memory_candidates(
            buffer_requirements.memoryTypeBits,
            memory_properties);
    const auto readback_memory_type =
        select_vulkan_offscreen_readback_memory_type(
            std::span{
                readback_candidates.data(),
                static_cast<std::size_t>(
                    memory_properties.memoryTypeCount)});
    if (!readback_memory_type.has_value()) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    no_host_visible_memory_type));
    }

    const VkMemoryAllocateInfo buffer_allocation_info{
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex =
            readback_memory_type.value(),
    };
    result =
        vkAllocateMemory(
            resources.device,
            &buffer_allocation_info,
            nullptr,
            &resources.staging_memory);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    memory_allocation_failure,
                result));
    }

    result =
        vkBindBufferMemory(
            resources.device,
            resources.staging_buffer,
            resources.staging_memory,
            0U);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    staging_memory_bind_failure,
                result));
    }

    result =
        vkMapMemory(
            resources.device,
            resources.staging_memory,
            0U,
            readback_size,
            0U,
            &resources.staging_mapped);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    memory_map_failure,
                result));
    }

    const VkCommandPoolCreateInfo command_pool_info{
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    command_buffer_allocation_failure,
                result));
    }

    const VkCommandBufferBeginInfo begin_info{
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
            &begin_info);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    command_buffer_begin_failure,
                result));
    }

    const VkImageSubresourceRange color_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0U,
        .levelCount = 1U,
        .baseArrayLayer = 0U,
        .layerCount = 1U,
    };

    const VkImageMemoryBarrier to_clear{
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0U,
        .dstAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .image = resources.image,
        .subresourceRange = color_range,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &to_clear);

    const VkClearColorValue clear{
        .float32 = {
            static_cast<float>(rgba[0]) / 255.0F,
            static_cast<float>(rgba[1]) / 255.0F,
            static_cast<float>(rgba[2]) / 255.0F,
            static_cast<float>(rgba[3]) / 255.0F,
        },
    };
    vkCmdClearColorImage(
        command_buffer,
        resources.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clear,
        1U,
        &color_range);

    const VkImageMemoryBarrier to_copy{
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout =
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout =
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex =
            VK_QUEUE_FAMILY_IGNORED,
        .image = resources.image,
        .subresourceRange = color_range,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0U,
        0U,
        nullptr,
        0U,
        nullptr,
        1U,
        &to_copy);

    const VkBufferImageCopy copy_region{
        .bufferOffset = 0U,
        .bufferRowLength = 0U,
        .bufferImageHeight = 0U,
        .imageSubresource =
            VkImageSubresourceLayers{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0U,
                .baseArrayLayer = 0U,
                .layerCount = 1U,
            },
        .imageOffset =
            VkOffset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .imageExtent =
            VkExtent3D{
                .width = plan->width,
                .height = plan->height,
                .depth = 1U,
            },
    };
    vkCmdCopyImageToBuffer(
        command_buffer,
        resources.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        resources.staging_buffer,
        1U,
        &copy_region);

    const VkBufferMemoryBarrier host_read{
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
        .buffer = resources.staging_buffer,
        .offset = 0U,
        .size = readback_size,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0U,
        0U,
        nullptr,
        1U,
        &host_read,
        0U,
        nullptr);

    result =
        vkEndCommandBuffer(
            command_buffer);
    if (result != VK_SUCCESS) {
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    fence_wait_failure,
                result));
    }

    const auto readback_index =
        readback_memory_type.value();
    const auto readback_flags =
        memory_properties.
            memoryTypes[readback_index].
            propertyFlags;
    const bool coherent =
        (readback_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) !=
        0U;
    if (!coherent) {
        const VkMappedMemoryRange range{
            .sType =
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory =
                resources.staging_memory,
            .offset = 0U,
            .size = VK_WHOLE_SIZE,
        };
        result =
            vkInvalidateMappedMemoryRanges(
                resources.device,
                1U,
                &range);
        if (result != VK_SUCCESS) {
            return VulkanOffscreenImageExecutionResult::failure(
                error(
                    VulkanOffscreenImageErrorCode::
                        memory_invalidate_failure,
                    result));
        }
    }

    try {
        std::vector<std::byte> logical_pixels(
            static_cast<std::size_t>(
                plan->logical_byte_count));
        std::memcpy(
            logical_pixels.data(),
            resources.staging_mapped,
            logical_pixels.size());

        return VulkanOffscreenImageExecutionResult::success(
            VulkanOffscreenImageExecution{
                .image_id = plan->image_id,
                .logical_pixels =
                    std::move(logical_pixels),
                .device =
                    VulkanOffscreenImageDeviceInfo{
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
        return VulkanOffscreenImageExecutionResult::failure(
            error(
                VulkanOffscreenImageErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::graphics
