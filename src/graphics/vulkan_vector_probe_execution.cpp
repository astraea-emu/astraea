#include <astraea/graphics/vulkan_vector_probe_execution.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <volk.h>

namespace astraea::graphics {
namespace {

[[nodiscard]] VulkanVectorProbeExecutionError error(
    VulkanVectorProbeExecutionErrorCode code,
    VkResult result = VK_SUCCESS) noexcept {
    return VulkanVectorProbeExecutionError{
        .code = code,
        .native_result =
            static_cast<std::int32_t>(result),
    };
}

struct VulkanResources {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDescriptorSetLayout descriptor_set_layout =
        VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
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
            if (shader_module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(
                    device,
                    shader_module,
                    nullptr);
            }
            if (descriptor_pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(
                    device,
                    descriptor_pool,
                    nullptr);
            }
            if (descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(
                    device,
                    descriptor_set_layout,
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

[[nodiscard]] astraea::core::Result<
    std::vector<VkExtensionProperties>,
    VulkanVectorProbeExecutionError>
enumerate_instance_extensions() {
    std::uint32_t count = 0;
    auto result =
        vkEnumerateInstanceExtensionProperties(
            nullptr,
            &count,
            nullptr);
    if (result != VK_SUCCESS) {
        return decltype(
            enumerate_instance_extensions())::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
                return decltype(
                    enumerate_instance_extensions())::failure(
                    error(
                        VulkanVectorProbeExecutionErrorCode::
                            instance_creation_failure,
                        result));
            }
            properties.resize(count);
        }
        return decltype(
            enumerate_instance_extensions())::success(
            std::move(properties));
    } catch (const std::bad_alloc&) {
        return decltype(
            enumerate_instance_extensions())::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    host_allocation_failure));
    }
}

[[nodiscard]] astraea::core::Result<
    std::vector<VkExtensionProperties>,
    VulkanVectorProbeExecutionError>
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
        return decltype(
            enumerate_device_extensions(
                physical_device))::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
                return decltype(
                    enumerate_device_extensions(
                        physical_device))::failure(
                    error(
                        VulkanVectorProbeExecutionErrorCode::
                            device_extension_enumeration_failure,
                        result));
            }
            properties.resize(count);
        }
        return decltype(
            enumerate_device_extensions(
                physical_device))::success(
            std::move(properties));
    } catch (const std::bad_alloc&) {
        return decltype(
            enumerate_device_extensions(
                physical_device))::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        VulkanVectorProbeExecutionError>;

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
            error(
                VulkanVectorProbeExecutionErrorCode::
                    physical_device_enumeration_failure,
                result));
    }
    if (device_count == 0U) {
        return PhysicalDeviceSelectionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
                    VulkanVectorProbeExecutionErrorCode::
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

            std::vector<
                VulkanVectorProbeQueueCandidate>
                candidates;
            candidates.reserve(
                queue_properties.size());
            for (const auto& queue :
                 queue_properties) {
                candidates.push_back(
                    VulkanVectorProbeQueueCandidate{
                        .supports_compute =
                            (queue.queueFlags &
                             VK_QUEUE_COMPUTE_BIT) != 0U,
                        .queue_count =
                            queue.queueCount,
                    });
            }

            const auto family =
                select_vulkan_vector_probe_queue_family(
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
                VulkanVectorProbeExecutionErrorCode::
                    host_allocation_failure));
    }

    return PhysicalDeviceSelectionResult::failure(
        error(
            VulkanVectorProbeExecutionErrorCode::
                no_compute_queue_family));
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
        VulkanVectorProbeMemoryCandidate,
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
            VulkanVectorProbeMemoryCandidate{
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
        select_vulkan_vector_probe_memory_type(
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

[[nodiscard]] bool valid_probe_layout(
    const SpirvVectorProbeModule& module) noexcept {
    const auto& layout = module.layout;
    if (layout.descriptor_set != 0U ||
        layout.binding != 0U ||
        layout.word_size_bytes !=
            sizeof(std::uint32_t)) {
        return false;
    }
    if (layout.wave_size != 32U &&
        layout.wave_size != 64U) {
        return false;
    }
    if (layout.vgpr_stride_words !=
        layout.wave_size) {
        return false;
    }
    if (layout.required_vgpr_count == 0U ||
        layout.required_state_word_count == 0U) {
        return false;
    }
    const auto expected =
        static_cast<std::uint64_t>(
            layout.required_vgpr_count) *
        static_cast<std::uint64_t>(
            layout.wave_size);
    return expected ==
           layout.required_state_word_count;
}

}  // namespace

std::optional<std::uint32_t>
select_vulkan_vector_probe_queue_family(
    std::span<const VulkanVectorProbeQueueCandidate>
        candidates) noexcept {
    for (std::size_t index = 0;
         index < candidates.size();
         ++index) {
        if (candidates[index].supports_compute &&
            candidates[index].queue_count != 0U &&
            index <=
                std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
select_vulkan_vector_probe_memory_type(
    std::span<const VulkanVectorProbeMemoryCandidate>
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

VulkanVectorProbeExecutionResult
execute_spirv_vector_probe_on_vulkan(
    const SpirvVectorProbeModule& module,
    std::span<const std::uint32_t> initial_words) {
    if (!valid_probe_layout(module)) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    invalid_probe_layout));
    }

    if (initial_words.size() !=
        module.layout.required_state_word_count) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    input_word_count_mismatch));
    }

    if (initial_words.size() >
        std::numeric_limits<std::size_t>::max() /
            sizeof(std::uint32_t)) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    size_overflow));
    }

    const auto byte_size =
        initial_words.size() *
        sizeof(std::uint32_t);
    if (byte_size == 0U ||
        static_cast<std::uintmax_t>(byte_size) >
            std::numeric_limits<VkDeviceSize>::max()) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    size_overflow));
    }

    auto result = volkInitialize();
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    loader_unavailable,
                result));
    }

    VulkanResources resources;

    auto instance_extensions =
        enumerate_instance_extensions();
    if (!instance_extensions.has_value()) {
        return VulkanVectorProbeExecutionResult::failure(
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    instance_creation_failure,
                result));
    }
    volkLoadInstance(resources.instance);

    auto selected =
        select_physical_device(resources.instance);
    if (!selected.has_value()) {
        return VulkanVectorProbeExecutionResult::failure(
            selected.error());
    }
    const auto physical =
        selected.value();

    auto device_extensions =
        enumerate_device_extensions(
            physical.physical_device);
    if (!device_extensions.has_value()) {
        return VulkanVectorProbeExecutionResult::failure(
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
            static_cast<VkDeviceSize>(byte_size),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    memory_map_failure,
                result));
    }
    std::memcpy(
        resources.mapped,
        initial_words.data(),
        byte_size);

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
            return VulkanVectorProbeExecutionResult::failure(
                error(
                    VulkanVectorProbeExecutionErrorCode::
                        memory_flush_failure,
                    result));
        }
    }

    const VkDescriptorSetLayoutBinding binding{
        .binding = module.layout.binding,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .pImmutableSamplers = nullptr,
    };
    const VkDescriptorSetLayoutCreateInfo
        set_layout_info{
            .sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .bindingCount = 1U,
            .pBindings = &binding,
        };
    result =
        vkCreateDescriptorSetLayout(
            resources.device,
            &set_layout_info,
            nullptr,
            &resources.descriptor_set_layout);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    descriptor_set_layout_creation_failure,
                result));
    }

    const VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1U,
    };
    const VkDescriptorPoolCreateInfo pool_info{
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .maxSets = 1U,
        .poolSizeCount = 1U,
        .pPoolSizes = &pool_size,
    };
    result =
        vkCreateDescriptorPool(
            resources.device,
            &pool_info,
            nullptr,
            &resources.descriptor_pool);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    descriptor_pool_creation_failure,
                result));
    }

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    const VkDescriptorSetAllocateInfo
        descriptor_allocate_info{
            .sType =
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool =
                resources.descriptor_pool,
            .descriptorSetCount = 1U,
            .pSetLayouts =
                &resources.descriptor_set_layout,
        };
    result =
        vkAllocateDescriptorSets(
            resources.device,
            &descriptor_allocate_info,
            &descriptor_set);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    descriptor_set_allocation_failure,
                result));
    }

    const VkDescriptorBufferInfo descriptor_buffer{
        .buffer = resources.buffer,
        .offset = 0U,
        .range =
            static_cast<VkDeviceSize>(byte_size),
    };
    const VkWriteDescriptorSet descriptor_write{
        .sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = descriptor_set,
        .dstBinding = module.layout.binding,
        .dstArrayElement = 0U,
        .descriptorCount = 1U,
        .descriptorType =
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pImageInfo = nullptr,
        .pBufferInfo = &descriptor_buffer,
        .pTexelBufferView = nullptr,
    };
    vkUpdateDescriptorSets(
        resources.device,
        1U,
        &descriptor_write,
        0U,
        nullptr);

    if (module.words.empty() ||
        module.words.size() >
            std::numeric_limits<std::size_t>::max() /
                sizeof(std::uint32_t)) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    invalid_probe_layout));
    }

    const VkShaderModuleCreateInfo shader_info{
        .sType =
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .codeSize =
            module.words.size() *
            sizeof(std::uint32_t),
        .pCode = module.words.data(),
    };
    result =
        vkCreateShaderModule(
            resources.device,
            &shader_info,
            nullptr,
            &resources.shader_module);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    shader_module_creation_failure,
                result));
    }

    const VkPipelineLayoutCreateInfo pipeline_layout_info{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .setLayoutCount = 1U,
        .pSetLayouts =
            &resources.descriptor_set_layout,
        .pushConstantRangeCount = 0U,
        .pPushConstantRanges = nullptr,
    };
    result =
        vkCreatePipelineLayout(
            resources.device,
            &pipeline_layout_info,
            nullptr,
            &resources.pipeline_layout);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    pipeline_layout_creation_failure,
                result));
    }

    const VkPipelineShaderStageCreateInfo stage_info{
        .sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = resources.shader_module,
        .pName = "main",
        .pSpecializationInfo = nullptr,
    };
    const VkComputePipelineCreateInfo pipeline_info{
        .sType =
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .stage = stage_info,
        .layout = resources.pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    result =
        vkCreateComputePipelines(
            resources.device,
            VK_NULL_HANDLE,
            1U,
            &pipeline_info,
            nullptr,
            &resources.pipeline);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    compute_pipeline_creation_failure,
                result));
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    command_buffer_begin_failure,
                result));
    }

    vkCmdBindPipeline(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        resources.pipeline);
    vkCmdBindDescriptorSets(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        resources.pipeline_layout,
        module.layout.descriptor_set,
        1U,
        &descriptor_set,
        0U,
        nullptr);
    vkCmdDispatch(
        command_buffer,
        1U,
        1U,
        1U);

    const VkMemoryBarrier memory_barrier{
        .sType =
            VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask =
            VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0U,
        1U,
        &memory_barrier,
        0U,
        nullptr,
        0U,
        nullptr);

    result =
        vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    queue_submit_failure,
                result));
    }

    result =
        vkWaitForFences(
            resources.device,
            1U,
            &resources.fence,
            VK_TRUE,
            std::numeric_limits<
                std::uint64_t>::max());
    if (result != VK_SUCCESS) {
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
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
            return VulkanVectorProbeExecutionResult::failure(
                error(
                    VulkanVectorProbeExecutionErrorCode::
                        memory_invalidate_failure,
                    result));
        }
    }

    try {
        std::vector<std::uint32_t> final_words(
            initial_words.size());
        std::memcpy(
            final_words.data(),
            resources.mapped,
            byte_size);

        return VulkanVectorProbeExecutionResult::success(
            VulkanVectorProbeExecution{
                .final_words =
                    std::move(final_words),
                .device =
                    VulkanVectorProbeDeviceInfo{
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
        return VulkanVectorProbeExecutionResult::failure(
            error(
                VulkanVectorProbeExecutionErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::graphics
