#include <astraea/graphics/vulkan_raster_probe_execution.hpp>

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

#include <astraea/graphics/spirv_raster_probe.hpp>

#include <volk.h>

namespace astraea::graphics {
namespace {

[[nodiscard]] VulkanRasterProbeError error(
    VulkanRasterProbeErrorCode code,
    VkResult result = VK_SUCCESS) noexcept {
    return VulkanRasterProbeError{
        .code = code,
        .native_result =
            static_cast<std::int32_t>(result),
        .image_plan_error = std::nullopt,
    };
}

[[nodiscard]] VulkanRasterProbeError plan_error(
    const VulkanOffscreenImageError& plan) noexcept {
    return VulkanRasterProbeError{
        .code = VulkanRasterProbeErrorCode::
            image_plan_failure,
        .native_result = plan.native_result,
        .image_plan_error = plan,
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
        VulkanRasterProbeError>;

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
                VulkanRasterProbeErrorCode::
                    instance_creation_failure,
                result));
    }

    try {
        std::vector<VkExtensionProperties> properties(
            count);
        if (count != 0U) {
            result =
                vkEnumerateInstanceExtensionProperties(
                    nullptr,
                    &count,
                    properties.data());
            if (result != VK_SUCCESS) {
                return ExtensionResult::failure(
                    error(
                        VulkanRasterProbeErrorCode::
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
                VulkanRasterProbeErrorCode::
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
                VulkanRasterProbeErrorCode::
                    device_extension_enumeration_failure,
                result));
    }

    try {
        std::vector<VkExtensionProperties> properties(
            count);
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
                        VulkanRasterProbeErrorCode::
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
                VulkanRasterProbeErrorCode::
                    host_allocation_failure));
    }
}

struct PhysicalSelection {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkPhysicalDeviceProperties properties{};
};

using PhysicalSelectionResult =
    astraea::core::Result<
        PhysicalSelection,
        VulkanRasterProbeError>;

[[nodiscard]] PhysicalSelectionResult
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
        return PhysicalSelectionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    physical_device_enumeration_failure,
                result));
    }
    if (device_count == 0U) {
        return PhysicalSelectionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
            return PhysicalSelectionResult::failure(
                error(
                    VulkanRasterProbeErrorCode::
                        physical_device_enumeration_failure,
                    result));
        }
        devices.resize(device_count);

        constexpr VkImageUsageFlags usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        for (const auto physical : devices) {
            VkImageFormatProperties image_properties{};
            const auto image_result =
                vkGetPhysicalDeviceImageFormatProperties(
                    physical,
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
                physical,
                &queue_count,
                nullptr);
            if (queue_count == 0U) {
                continue;
            }

            std::vector<VkQueueFamilyProperties>
                queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(
                physical,
                &queue_count,
                queues.data());
            queues.resize(queue_count);

            std::vector<VulkanOffscreenImageQueueCandidate>
                candidates;
            candidates.reserve(queues.size());
            for (const auto& queue : queues) {
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
                physical,
                &properties);

            return PhysicalSelectionResult::success(
                PhysicalSelection{
                    .device = physical,
                    .queue_family = family.value(),
                    .properties = properties,
                });
        }
    } catch (const std::bad_alloc&) {
        return PhysicalSelectionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    host_allocation_failure));
    }

    return PhysicalSelectionResult::failure(
        error(
            VulkanRasterProbeErrorCode::
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
    for (std::uint32_t index = 0U;
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
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U,
                .host_visible =
                    (flags &
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U,
                .host_coherent =
                    (flags &
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U,
            };
    }
    return candidates;
}

struct Resources {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    void* staging_mapped = nullptr;
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    Resources() = default;
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources() {
        if (device != VK_NULL_HANDLE) {
            if (staging_mapped != nullptr &&
                staging_memory != VK_NULL_HANDLE) {
                vkUnmapMemory(device, staging_memory);
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
            if (pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(
                    device,
                    pipeline,
                    nullptr);
            }
            if (pipeline_layout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(
                    device,
                    pipeline_layout,
                    nullptr);
            }
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(
                    device,
                    framebuffer,
                    nullptr);
            }
            if (render_pass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(
                    device,
                    render_pass,
                    nullptr);
            }
            if (vertex_shader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    device,
                    vertex_shader,
                    nullptr);
            }
            if (fragment_shader != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    device,
                    fragment_shader,
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
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

[[nodiscard]] VkResult create_shader_module(
    VkDevice device,
    std::span<const std::uint32_t> words,
    VkShaderModule* shader) noexcept {
    const VkShaderModuleCreateInfo info{
        .sType =
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .codeSize =
            words.size_bytes(),
        .pCode = words.data(),
    };
    return vkCreateShaderModule(
        device,
        &info,
        nullptr,
        shader);
}

}  // namespace

VulkanRasterProbeExecutionResult
execute_vulkan_fullscreen_triangle_spirv(
    const GuestGpuImageView& image,
    std::span<const std::uint32_t> vertex_words,
    std::span<const std::uint32_t> fragment_words) {
    const auto plan =
        plan_vulkan_offscreen_image(image);
    if (!plan.has_value()) {
        return VulkanRasterProbeExecutionResult::failure(
            plan_error(plan.error()));
    }

    auto result = volkInitialize();
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    loader_unavailable,
                result));
    }

    Resources resources;

    auto instance_extensions =
        enumerate_instance_extensions();
    if (!instance_extensions.has_value()) {
        return VulkanRasterProbeExecutionResult::failure(
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
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Astraea",
        .applicationVersion = 0U,
        .pEngineName = "Astraea",
        .engineVersion = 0U,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo instance_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            selected.error());
    }
    const auto physical = selected.value();

    auto device_extensions =
        enumerate_device_extensions(physical.device);
    if (!device_extensions.has_value()) {
        return VulkanRasterProbeExecutionResult::failure(
            device_extensions.error());
    }

    std::vector<const char*> enabled_device_extensions;
#ifdef VK_KHR_portability_subset
    if (has_extension(
            device_extensions.value(),
            VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
        enabled_device_extensions.push_back(
            VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    }
#endif

    constexpr float priority = 1.0F;
    const VkDeviceQueueCreateInfo queue_info{
        .sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueFamilyIndex = physical.queue_family,
        .queueCount = 1U,
        .pQueuePriorities = &priority,
    };
    const VkDeviceCreateInfo device_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
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
            physical.device,
            &device_info,
            nullptr,
            &resources.device);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    device_creation_failure,
                result));
    }
    volkLoadDevice(resources.device);

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(
        resources.device,
        physical.queue_family,
        0U,
        &queue);

    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
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
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        physical.device,
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    no_device_memory_type));
    }

    const VkMemoryAllocateInfo image_alloc{
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = image_requirements.size,
        .memoryTypeIndex = image_memory_type.value(),
    };
    result =
        vkAllocateMemory(
            resources.device,
            &image_alloc,
            nullptr,
            &resources.image_memory);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    staging_buffer_creation_failure,
                result));
    }

    VkMemoryRequirements staging_requirements{};
    vkGetBufferMemoryRequirements(
        resources.device,
        resources.staging_buffer,
        &staging_requirements);
    const auto staging_candidates =
        memory_candidates(
            staging_requirements.memoryTypeBits,
            memory_properties);
    const auto staging_memory_type =
        select_vulkan_offscreen_readback_memory_type(
            std::span{
                staging_candidates.data(),
                static_cast<std::size_t>(
                    memory_properties.memoryTypeCount)});
    if (!staging_memory_type.has_value()) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    no_host_visible_memory_type));
    }

    const VkMemoryAllocateInfo staging_alloc{
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = staging_requirements.size,
        .memoryTypeIndex = staging_memory_type.value(),
    };
    result =
        vkAllocateMemory(
            resources.device,
            &staging_alloc,
            nullptr,
            &resources.staging_memory);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    memory_map_failure,
                result));
    }

    result =
        create_shader_module(
            resources.device,
            vertex_words,
            &resources.vertex_shader);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    shader_module_creation_failure,
                result));
    }
    result =
        create_shader_module(
            resources.device,
            fragment_words,
            &resources.fragment_shader);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    shader_module_creation_failure,
                result));
    }

    const VkAttachmentDescription attachment{
        .flags = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp =
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp =
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference color_reference{
        .attachment = 0U,
        .layout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass{
        .flags = 0U,
        .pipelineBindPoint =
            VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0U,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1U,
        .pColorAttachments = &color_reference,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0U,
        .pPreserveAttachments = nullptr,
    };
    const VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0U,
        .srcStageMask =
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0U,
        .dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0U,
    };
    const VkRenderPassCreateInfo render_pass_info{
        .sType =
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .attachmentCount = 1U,
        .pAttachments = &attachment,
        .subpassCount = 1U,
        .pSubpasses = &subpass,
        .dependencyCount = 1U,
        .pDependencies = &dependency,
    };
    result =
        vkCreateRenderPass(
            resources.device,
            &render_pass_info,
            nullptr,
            &resources.render_pass);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    render_pass_creation_failure,
                result));
    }

    const VkFramebufferCreateInfo framebuffer_info{
        .sType =
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .renderPass = resources.render_pass,
        .attachmentCount = 1U,
        .pAttachments = &resources.image_view,
        .width = plan->width,
        .height = plan->height,
        .layers = 1U,
    };
    result =
        vkCreateFramebuffer(
            resources.device,
            &framebuffer_info,
            nullptr,
            &resources.framebuffer);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    framebuffer_creation_failure,
                result));
    }

    const VkPipelineLayoutCreateInfo layout_info{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .setLayoutCount = 0U,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0U,
        .pPushConstantRanges = nullptr,
    };
    result =
        vkCreatePipelineLayout(
            resources.device,
            &layout_info,
            nullptr,
            &resources.pipeline_layout);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    pipeline_layout_creation_failure,
                result));
    }

    const std::array<VkPipelineShaderStageCreateInfo, 2>
        stages{
            VkPipelineShaderStageCreateInfo{
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = resources.vertex_shader,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
            VkPipelineShaderStageCreateInfo{
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0U,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = resources.fragment_shader,
                .pName = "main",
                .pSpecializationInfo = nullptr,
            },
        };

    const VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .vertexBindingDescriptionCount = 0U,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0U,
        .pVertexAttributeDescriptions = nullptr,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    const VkViewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(plan->width),
        .height = static_cast<float>(plan->height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkRect2D scissor{
        .offset = VkOffset2D{.x = 0, .y = 0},
        .extent =
            VkExtent2D{
                .width = plan->width,
                .height = plan->height,
            },
    };
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .viewportCount = 1U,
        .pViewports = &viewport,
        .scissorCount = 1U,
        .pScissors = &scissor,
    };
    const VkPipelineRasterizationStateCreateInfo raster{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0F,
        .depthBiasClamp = 0.0F,
        .depthBiasSlopeFactor = 0.0F,
        .lineWidth = 1.0F,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    const VkPipelineColorBlendAttachmentState blend_attachment{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blend{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1U,
        .pAttachments = &blend_attachment,
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F},
    };
    const VkGraphicsPipelineCreateInfo pipeline_info{
        .sType =
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .stageCount =
            static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &blend,
        .pDynamicState = nullptr,
        .layout = resources.pipeline_layout,
        .renderPass = resources.render_pass,
        .subpass = 0U,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    result =
        vkCreateGraphicsPipelines(
            resources.device,
            VK_NULL_HANDLE,
            1U,
            &pipeline_info,
            nullptr,
            &resources.pipeline);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    graphics_pipeline_creation_failure,
                result));
    }

    const VkCommandPoolCreateInfo pool_info{
        .sType =
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = physical.queue_family,
    };
    result =
        vkCreateCommandPool(
            resources.device,
            &pool_info,
            nullptr,
            &resources.command_pool);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    command_pool_creation_failure,
                result));
    }

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo command_alloc{
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
            &command_alloc,
            &command_buffer);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    command_buffer_begin_failure,
                result));
    }

    const VkClearValue clear{
        .color =
            VkClearColorValue{
                .float32 =
                    {0.0F, 0.0F, 0.0F, 1.0F},
            },
    };
    const VkRenderPassBeginInfo render_begin{
        .sType =
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = resources.render_pass,
        .framebuffer = resources.framebuffer,
        .renderArea =
            VkRect2D{
                .offset =
                    VkOffset2D{
                        .x = 0,
                        .y = 0,
                    },
                .extent =
                    VkExtent2D{
                        .width = plan->width,
                        .height = plan->height,
                    },
            },
        .clearValueCount = 1U,
        .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(
        command_buffer,
        &render_begin,
        VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
        command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        resources.pipeline);
    vkCmdDraw(
        command_buffer,
        3U,
        1U,
        0U,
        0U);
    vkCmdEndRenderPass(command_buffer);

    const VkImageSubresourceRange color_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0U,
        .levelCount = 1U,
        .baseArrayLayer = 0U,
        .layerCount = 1U,
    };
    const VkImageMemoryBarrier to_copy{
        .sType =
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout =
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
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

    result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    command_buffer_end_failure,
                result));
    }

    const VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    fence_creation_failure,
                result));
    }

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
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
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    fence_wait_failure,
                result));
    }

    const auto staging_index =
        staging_memory_type.value();
    const auto staging_flags =
        memory_properties.
            memoryTypes[staging_index].
            propertyFlags;
    const bool coherent =
        (staging_flags &
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0U;
    if (!coherent) {
        const VkMappedMemoryRange range{
            .sType =
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = resources.staging_memory,
            .offset = 0U,
            .size = VK_WHOLE_SIZE,
        };
        result =
            vkInvalidateMappedMemoryRanges(
                resources.device,
                1U,
                &range);
        if (result != VK_SUCCESS) {
            return VulkanRasterProbeExecutionResult::failure(
                error(
                    VulkanRasterProbeErrorCode::
                        memory_invalidate_failure,
                    result));
        }
    }

    try {
        std::vector<std::byte> pixels(
            static_cast<std::size_t>(
                plan->logical_byte_count));
        std::memcpy(
            pixels.data(),
            resources.staging_mapped,
            pixels.size());

        return VulkanRasterProbeExecutionResult::success(
            VulkanRasterProbeExecution{
                .image_id = plan->image_id,
                .logical_pixels = std::move(pixels),
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
                .vertex_count = 3U,
            });
    } catch (const std::bad_alloc&) {
        return VulkanRasterProbeExecutionResult::failure(
            error(
                VulkanRasterProbeErrorCode::
                    host_allocation_failure));
    }
}

VulkanRasterProbeExecutionResult
execute_vulkan_fullscreen_triangle_probe(
    const GuestGpuImageView& image) {
    const auto modules =
        build_spirv_raster_probe_modules();
    return execute_vulkan_fullscreen_triangle_spirv(
        image,
        modules.vertex_words,
        modules.fragment_words);
}

}  // namespace astraea::graphics
