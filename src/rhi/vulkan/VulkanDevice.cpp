#include "rhi/vulkan/VulkanDevice.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/Buffer.h"
#include "rhi/vulkan/DescriptorAllocator.h"
#include "rhi/vulkan/GpuAllocator.h"
#include "rhi/vulkan/ShaderModule.h"
#include "rhi/vulkan/VulkanGraphicsPipeline.h"

#include <algorithm>
#include <array>
#include <cstring>
#if defined(MINI_DEBUG)
#include <iostream>
#endif
#include <iterator>
#include <set>
#include <string>
#include <utility>

namespace engine::rhi::vulkan {
namespace {

#if defined(MINI_DEBUG)
constexpr std::array kValidationLayers{"VK_LAYER_KHRONOS_validation"};
#endif
constexpr std::array kDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        Log::fatal("VulkanDevice",
                   std::string(operation) + " failed (VkResult " +
                       std::to_string(static_cast<int>(result)) + ")");
    }
}

#if defined(MINI_DEBUG)
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan] " << data->pMessage << '\n';
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}
#endif

VkBufferUsageFlags toVulkan(BufferUsage usage) {
    VkBufferUsageFlags result = 0;
    if (hasFlag(usage, BufferUsage::Vertex))
        result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Index))
        result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Uniform))
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Storage))
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::TransferSource))
        result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, BufferUsage::TransferDestination))
        result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return result;
}

VkDescriptorType toVulkan(BindingType type) {
    switch (type) {
    case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    Log::fatal("VulkanDevice", "Unsupported RHI binding type");
}

VkShaderStageFlags toVulkan(ShaderVisibility visibility) {
    VkShaderStageFlags result{};
    if (hasFlag(visibility, ShaderVisibility::Vertex))
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasFlag(visibility, ShaderVisibility::Fragment))
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    return result;
}

std::pair<VmaMemoryUsage, VmaAllocationCreateFlags> toVulkan(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::DeviceLocal: return {VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0};
    case MemoryUsage::Upload:
        return {VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT};
    case MemoryUsage::Readback:
        return {VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT};
    }
    return {VMA_MEMORY_USAGE_AUTO, 0};
}

template <typename Slots> auto reusableSlot(Slots& slots) {
    auto found = std::ranges::find_if(slots, [](const auto& slot) { return !slot.resource; });
    if (found == slots.end()) {
        slots.emplace_back();
        found = std::prev(slots.end());
    }
    return found;
}

} // namespace

VulkanDevice::VulkanDevice(void* nativeInstance, void* nativeWindow) {
    createInstance();
    createDebugMessenger();
    createSurface(nativeInstance, nativeWindow);
    selectPhysicalDevice();
    createLogicalDevice();
    allocator_ = std::make_unique<::engine::GpuAllocator>(instance_, physicalDevice_, device_);
    descriptorAllocator_ = std::make_unique<::engine::DescriptorAllocator>(device_, 256);
    createCommandPool();
}

VulkanDevice::~VulkanDevice() {
    waitIdle();
    clear();
    allocator_.reset();
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
#if defined(MINI_DEBUG)
    if (debugMessenger_ != VK_NULL_HANDLE) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(instance_, debugMessenger_, nullptr);
    }
#endif
    vkDestroyInstance(instance_, nullptr);
}

void VulkanDevice::createInstance() {
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Mini Vulkan Engine";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.pEngineName = "MiniVulkanEngine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> extensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
#if defined(MINI_DEBUG)
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
#if defined(MINI_DEBUG)
    auto debugInfo = debugCreateInfo();
    std::uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    const bool found = std::ranges::any_of(availableLayers, [](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, kValidationLayers[0]) == 0;
    });
    if (!found) {
        Log::fatal("VulkanDevice",
                   "Vulkan validation layer is unavailable; install the Vulkan SDK");
    }
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(kValidationLayers.size());
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
    createInfo.pNext = &debugInfo;
#endif
    check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

void VulkanDevice::createDebugMessenger() {
#if defined(MINI_DEBUG)
    const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!create) {
        Log::fatal("VulkanDevice", "VK_EXT_debug_utils is unavailable");
    }
    auto info = debugCreateInfo();
    check(create(instance_, &info, nullptr, &debugMessenger_), "vkCreateDebugUtilsMessengerEXT");
#endif
}

void VulkanDevice::createSurface(void* nativeInstance, void* nativeWindow) {
    if (!nativeInstance || !nativeWindow) {
        Log::fatal("VulkanDevice", "Invalid native window handles");
    }
    VkWin32SurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    createInfo.hinstance = static_cast<HINSTANCE>(nativeInstance);
    createInfo.hwnd = static_cast<HWND>(nativeWindow);
    check(vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_),
          "vkCreateWin32SurfaceKHR");
}

VulkanDevice::QueueFamilies VulkanDevice::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilies result;
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    for (std::uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            result.graphics = i;
            result.hasGraphics = true;
        }
        VkBool32 supportsPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &supportsPresent);
        if (supportsPresent == VK_TRUE) {
            result.present = i;
            result.hasPresent = true;
        }
        if (result.complete())
            break;
    }
    return result;
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) const {
    if (!findQueueFamilies(device).complete())
        return false;
    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());
    for (const char* required : kDeviceExtensions) {
        if (!std::ranges::any_of(available, [required](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, required) == 0;
            })) {
            return false;
        }
    }
    std::uint32_t formatCount = 0;
    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    VkPhysicalDeviceVulkan13Features features13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    return formatCount > 0 && presentModeCount > 0 && features13.dynamicRendering;
}

void VulkanDevice::selectPhysicalDevice() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice candidate : devices) {
        if (isDeviceSuitable(candidate)) {
            physicalDevice_ = candidate;
            break;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanDevice",
                   "No Vulkan 1.3 GPU with swapchain and dynamic rendering support found");
    }
}

void VulkanDevice::createLogicalDevice() {
    const QueueFamilies families = findQueueFamilies(physicalDevice_);
    graphicsQueueFamily_ = families.graphics;
    presentQueueFamily_ = families.present;
    const std::set uniqueFamilies{graphicsQueueFamily_, presentQueueFamily_};
    constexpr float priority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (std::uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queueInfos.push_back(info);
    }
    VkPhysicalDeviceVulkan13Features features13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
}

void VulkanDevice::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool");
}

VmaAllocator VulkanDevice::allocator() const {
    return allocator_->handle();
}

BufferHandle VulkanDevice::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0 || desc.usage == BufferUsage::None) {
        Log::fatal("VulkanDevice", "Invalid buffer description");
    }
    auto slot = reusableSlot(buffers_);
    const auto [memoryUsage, allocationFlags] = toVulkan(desc.memoryUsage);
    slot->resource = std::make_unique<::engine::Buffer>(
        allocator_->handle(), desc.size, toVulkan(desc.usage), memoryUsage, allocationFlags);
    slot->memoryUsage = desc.memoryUsage;
    return {static_cast<std::uint32_t>(std::distance(buffers_.begin(), slot)), slot->generation};
}

void VulkanDevice::destroyBuffer(BufferHandle handle) {
    if (handle.index >= buffers_.size())
        return;
    BufferSlot& slot = buffers_[handle.index];
    if (!slot.resource || slot.generation != handle.generation)
        return;
    slot.resource.reset();
    ++slot.generation;
}

void VulkanDevice::uploadBuffer(BufferHandle destination,
                                std::span<const std::byte> data,
                                std::uint64_t offset) {
    Buffer& target = requireBuffer(destination);
    if (data.empty() || offset > target.size() || data.size_bytes() > target.size() - offset) {
        Log::fatal("VulkanDevice", "Invalid buffer upload range");
    }

    if (buffers_[destination.index].memoryUsage == MemoryUsage::Upload) {
        target.upload(data, offset);
        return;
    }

    Buffer staging{allocator_->handle(),
                   data.size_bytes(),
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_AUTO,
                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT};
    staging.upload(data);

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
          "vkAllocateCommandBuffers(upload)");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(upload)");
    const VkBufferCopy copy{0, offset, data.size_bytes()};
    vkCmdCopyBuffer(commandBuffer, staging.handle(), target.handle(), 1, &copy);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(upload)");

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit(upload)");
    check(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle(upload)");
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc& desc) {
    if (desc.bytecode.empty()) {
        Log::fatal("VulkanDevice", "Cannot create an empty shader");
    }
    auto slot = reusableSlot(shaders_);
    slot->resource =
        std::make_unique<::engine::ShaderModule>(device_, desc.bytecode, desc.debugName);
    return {static_cast<std::uint32_t>(std::distance(shaders_.begin(), slot)), slot->generation};
}

void VulkanDevice::destroyShader(ShaderHandle handle) {
    if (handle.index >= shaders_.size())
        return;
    ShaderSlot& slot = shaders_[handle.index];
    if (!slot.resource || slot.generation != handle.generation)
        return;
    slot.resource.reset();
    ++slot.generation;
}

GraphicsPipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (!desc.vertexShader || !desc.fragmentShader || desc.colorFormats.empty()) {
        Log::fatal("VulkanDevice", "Invalid graphics pipeline description");
    }
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(desc.bindGroupLayouts.size());
    for (BindGroupLayoutHandle layout : desc.bindGroupLayouts) {
        layouts.push_back(resolveBindGroupLayout(layout));
    }
    auto slot = reusableSlot(pipelines_);
    slot->resource = std::make_unique<VulkanGraphicsPipeline>(device_,
                                                              desc,
                                                              resolveShader(desc.vertexShader),
                                                              resolveShader(desc.fragmentShader),
                                                              layouts);
    return {static_cast<std::uint32_t>(std::distance(pipelines_.begin(), slot)), slot->generation};
}

void VulkanDevice::destroyGraphicsPipeline(GraphicsPipelineHandle handle) {
    if (handle.index >= pipelines_.size())
        return;
    PipelineSlot& slot = pipelines_[handle.index];
    if (!slot.resource || slot.generation != handle.generation)
        return;
    slot.resource.reset();
    ++slot.generation;
}

BindGroupLayoutHandle VulkanDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (desc.entries.empty()) {
        Log::fatal("VulkanDevice", "Cannot create an empty bind group layout");
    }
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());
    for (const BindGroupLayoutEntry& entry : desc.entries) {
        if (entry.visibility == ShaderVisibility::None) {
            Log::fatal("VulkanDevice", "Bind group layout entry has no shader visibility");
        }
        bindings.push_back(
            {entry.binding, toVulkan(entry.type), 1, toVulkan(entry.visibility), nullptr});
    }
    auto slot = reusableSlot(bindGroupLayouts_);
    slot->resource = std::make_unique<::engine::DescriptorSetLayout>(device_, bindings);
    return {static_cast<std::uint32_t>(std::distance(bindGroupLayouts_.begin(), slot)),
            slot->generation};
}

void VulkanDevice::destroyBindGroupLayout(BindGroupLayoutHandle handle) {
    if (handle.index >= bindGroupLayouts_.size())
        return;
    BindGroupLayoutSlot& slot = bindGroupLayouts_[handle.index];
    if (!slot.resource || slot.generation != handle.generation)
        return;
    slot.resource.reset();
    ++slot.generation;
}

BindGroupHandle VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
    const VkDescriptorSet descriptor =
        descriptorAllocator_->allocate(resolveBindGroupLayout(desc.layout));
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(desc.entries.size());
    writes.reserve(desc.entries.size());
    for (const BindGroupEntry& entry : desc.entries) {
        if (entry.type == BindingType::SampledTexture) {
            Log::fatal("VulkanDevice", "Sampled texture bind groups require texture resources");
        }
        const Buffer& buffer = requireBuffer(entry.buffer);
        const std::uint64_t size = entry.size == 0 ? buffer.size() - entry.offset : entry.size;
        if (entry.offset > buffer.size() || size > buffer.size() - entry.offset) {
            Log::fatal("VulkanDevice", "Invalid bind group buffer range");
        }
        bufferInfos.push_back({buffer.handle(), entry.offset, size});
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = toVulkan(entry.type);
        write.pBufferInfo = &bufferInfos.back();
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(
        device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    auto slot = reusableSlot(bindGroups_);
    slot->resource = descriptor;
    return {static_cast<std::uint32_t>(std::distance(bindGroups_.begin(), slot)), slot->generation};
}

void VulkanDevice::destroyBindGroup(BindGroupHandle handle) {
    if (handle.index >= bindGroups_.size())
        return;
    BindGroupSlot& slot = bindGroups_[handle.index];
    if (slot.resource == VK_NULL_HANDLE || slot.generation != handle.generation)
        return;
    descriptorAllocator_->free(slot.resource);
    slot.resource = VK_NULL_HANDLE;
    ++slot.generation;
}

void VulkanDevice::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

Buffer& VulkanDevice::requireBuffer(BufferHandle handle) {
    return const_cast<Buffer&>(std::as_const(*this).requireBuffer(handle));
}

const Buffer& VulkanDevice::requireBuffer(BufferHandle handle) const {
    if (handle.index >= buffers_.size()) {
        Log::fatal("VulkanDevice", "Invalid RHI buffer handle");
    }
    const BufferSlot& slot = buffers_[handle.index];
    if (!slot.resource || slot.generation != handle.generation) {
        Log::fatal("VulkanDevice", "Stale RHI buffer handle");
    }
    return *slot.resource;
}

VkBuffer VulkanDevice::resolveBuffer(BufferHandle handle) const {
    return requireBuffer(handle).handle();
}

VkImage VulkanDevice::resolveTexture(TextureHandle) const {
    Log::fatal("VulkanDevice", "Texture handle is not owned by this device resolver");
}

VkImageView VulkanDevice::resolveTextureView(TextureViewHandle) const {
    Log::fatal("VulkanDevice", "Texture view handle is not owned by this device resolver");
}

VkShaderModule VulkanDevice::resolveShader(ShaderHandle handle) const {
    if (handle.index >= shaders_.size()) {
        Log::fatal("VulkanDevice", "Invalid RHI shader handle");
    }
    const ShaderSlot& slot = shaders_[handle.index];
    if (!slot.resource || slot.generation != handle.generation) {
        Log::fatal("VulkanDevice", "Stale RHI shader handle");
    }
    return slot.resource->handle();
}

VkDescriptorSetLayout VulkanDevice::resolveBindGroupLayout(BindGroupLayoutHandle handle) const {
    if (handle.index >= bindGroupLayouts_.size()) {
        Log::fatal("VulkanDevice", "Invalid RHI bind group layout handle");
    }
    const BindGroupLayoutSlot& slot = bindGroupLayouts_[handle.index];
    if (!slot.resource || slot.generation != handle.generation) {
        Log::fatal("VulkanDevice", "Stale RHI bind group layout handle");
    }
    return slot.resource->handle();
}

ResolvedPipeline VulkanDevice::resolvePipeline(GraphicsPipelineHandle handle) const {
    if (handle.index >= pipelines_.size()) {
        Log::fatal("VulkanDevice", "Invalid RHI graphics pipeline handle");
    }
    const PipelineSlot& slot = pipelines_[handle.index];
    if (!slot.resource || slot.generation != handle.generation) {
        Log::fatal("VulkanDevice", "Stale RHI graphics pipeline handle");
    }
    return {slot.resource->handle(), slot.resource->layout()};
}

VkDescriptorSet VulkanDevice::resolveBindGroup(BindGroupHandle handle) const {
    if (handle.index >= bindGroups_.size()) {
        Log::fatal("VulkanDevice", "Invalid RHI bind group handle");
    }
    const BindGroupSlot& slot = bindGroups_[handle.index];
    if (slot.resource == VK_NULL_HANDLE || slot.generation != handle.generation) {
        Log::fatal("VulkanDevice", "Stale RHI bind group handle");
    }
    return slot.resource;
}

void VulkanDevice::clear() {
    pipelines_.clear();
    bindGroups_.clear();
    descriptorAllocator_.reset();
    bindGroupLayouts_.clear();
    shaders_.clear();
    buffers_.clear();
}

} // namespace engine::rhi::vulkan
