#include "rhi/vulkan/VulkanDevice.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDescriptorAllocator.h"
#include "rhi/vulkan/VulkanGraphicsPipeline.h"
#include "rhi/vulkan/VulkanImage.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanShaderModule.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#if defined(MINI_DEBUG)
#include <iostream>
#endif
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

VkFormat toVulkan(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case TextureFormat::Undefined: break;
    }
    Log::fatal("VulkanDevice", "Unsupported Texture format");
}

VkImageUsageFlags toVulkan(TextureUsage usage) {
    VkImageUsageFlags result{};
    if (hasFlag(usage, TextureUsage::Sampled))
        result |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasFlag(usage, TextureUsage::TransferSource))
        result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, TextureUsage::TransferDestination))
        result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return result;
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

} // namespace

VulkanDevice::VulkanDevice(const SurfaceSource& surface) {
    createInstance();
    createDebugMessenger();
    createSurface(surface);
    selectPhysicalDevice();
    createLogicalDevice();
    createAllocator();
    descriptorAllocator_ = std::make_unique<VulkanDescriptorAllocator>(device_, 256);
    createCommandPool();
}

VulkanDevice::~VulkanDevice() {
    waitIdle();
    clear();
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
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

void VulkanDevice::createSurface(const SurfaceSource& surface) {
    if (surface.windowSystem != WindowSystem::Win32) {
        Log::fatal("VulkanDevice", "Unsupported native window system");
    }
    if (!surface.nativeDisplay || !surface.nativeWindow) {
        Log::fatal("VulkanDevice", "Invalid native window handles");
    }
    VkWin32SurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    createInfo.hinstance = static_cast<HINSTANCE>(surface.nativeDisplay);
    createInfo.hwnd = static_cast<HWND>(surface.nativeWindow);
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

void VulkanDevice::createAllocator() {
    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = instance_;
    createInfo.physicalDevice = physicalDevice_;
    createInfo.device = device_;
    createInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    check(vmaCreateAllocator(&createInfo, &allocator_), "vmaCreateAllocator");
}

VmaAllocator VulkanDevice::allocator() const {
    return allocator_;
}

BufferHandle VulkanDevice::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0 || desc.usage == BufferUsage::None) {
        Log::fatal("VulkanDevice", "Invalid buffer description");
    }
    const auto [memoryUsage, allocationFlags] = toVulkan(desc.memoryUsage);
    return buffers_.insert(BufferResource{
        std::make_unique<VulkanBuffer>(
            allocator_, desc.size, toVulkan(desc.usage), memoryUsage, allocationFlags),
        desc.memoryUsage,
    });
}

void VulkanDevice::destroyBuffer(BufferHandle handle) {
    (void)buffers_.release(handle);
}

void VulkanDevice::uploadBuffer(BufferHandle destination,
                                std::span<const std::byte> data,
                                std::uint64_t offset) {
    VulkanBuffer& target = requireBuffer(destination);
    if (data.empty() || offset > target.size() || data.size_bytes() > target.size() - offset) {
        Log::fatal("VulkanDevice", "Invalid buffer upload range");
    }

    if (requireBufferResource(destination).memoryUsage == MemoryUsage::Upload) {
        target.upload(data, offset);
        return;
    }

    VulkanBuffer staging{allocator_,
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

VkImage VulkanDevice::TextureResource::handle() const {
    return owned ? owned->handle() : external;
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& desc) {
    if (desc.dimension != TextureDimension::Texture2D || desc.format == TextureFormat::Undefined ||
        desc.width == 0 || desc.height == 0 || desc.depth != 1 || desc.mipCount == 0 ||
        desc.usage == TextureUsage::None) {
        Log::fatal("VulkanDevice", "Invalid Texture description");
    }
    auto image = std::make_unique<VulkanImage>(allocator_,
                                               VkExtent3D{desc.width, desc.height, desc.depth},
                                               toVulkan(desc.format),
                                               toVulkan(desc.usage),
                                               desc.mipCount);
    return textures_.insert(TextureResource{std::move(image), VK_NULL_HANDLE, false});
}

void VulkanDevice::destroyTexture(TextureHandle handle) {
    (void)textures_.release(handle);
}

void VulkanDevice::uploadTexture(TextureHandle destination,
                                 std::span<const TextureUploadRegion> regions) {
    TextureResource* resource = textures_.find(destination);
    if (!resource || !resource->owned || resource->uploaded || regions.empty())
        Log::fatal("VulkanDevice", "Invalid Texture upload");
    const VulkanImage& image = *resource->owned;
    if (regions.size() != image.mipCount())
        Log::fatal("VulkanDevice", "Texture upload must provide the complete mip chain");
    std::uint64_t totalSize{};
    std::vector<bool> suppliedMips(image.mipCount());
    for (const TextureUploadRegion& region : regions) {
        if (region.data.empty() || region.mipLevel >= image.mipCount() || region.arrayLayer != 0 ||
            suppliedMips[region.mipLevel]) {
            Log::fatal("VulkanDevice", "Invalid Texture upload region");
        }
        const std::uint32_t expectedWidth = std::max(1U, image.width() >> region.mipLevel);
        const std::uint32_t expectedHeight = std::max(1U, image.height() >> region.mipLevel);
        const std::uint64_t expectedSize =
            static_cast<std::uint64_t>(expectedWidth) * expectedHeight * 4U;
        if (region.width != expectedWidth || region.height != expectedHeight ||
            region.data.size_bytes() != expectedSize ||
            totalSize > std::numeric_limits<std::uint64_t>::max() - expectedSize) {
            Log::fatal("VulkanDevice", "Texture upload region does not match the mip level");
        }
        suppliedMips[region.mipLevel] = true;
        totalSize += region.data.size_bytes();
    }
    VulkanBuffer staging{allocator_,
                         totalSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_AUTO,
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT};
    std::uint64_t stagingOffset{};
    std::vector<VkBufferImageCopy> copies;
    copies.reserve(regions.size());
    for (const TextureUploadRegion& region : regions) {
        staging.upload(region.data, stagingOffset);
        VkBufferImageCopy copy{};
        copy.bufferOffset = stagingOffset;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = region.mipLevel;
        copy.imageSubresource.baseArrayLayer = region.arrayLayer;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {region.width, region.height, 1};
        copies.push_back(copy);
        stagingOffset += region.data.size_bytes();
    }

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
          "vkAllocateCommandBuffers(texture upload)");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(texture upload)");

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image.handle();
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = image.mipCount();
    toTransfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &toTransfer);
    vkCmdCopyBufferToImage(commandBuffer,
                           staging.handle(),
                           image.handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(copies.size()),
                           copies.data());
    VkImageMemoryBarrier toShaderRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = image.handle();
    toShaderRead.subresourceRange = toTransfer.subresourceRange;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &toShaderRead);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(texture upload)");
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE),
          "vkQueueSubmit(texture upload)");
    check(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle(texture upload)");
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    resource->uploaded = true;
}

TextureViewHandle VulkanDevice::createTextureView(const TextureViewDesc& desc) {
    const TextureResource* texture = textures_.find(desc.texture);
    if (!texture || desc.format == TextureFormat::Undefined || desc.mipCount == 0)
        Log::fatal("VulkanDevice", "Invalid TextureView description");
    if (texture->owned && (desc.format != texture->owned->format() ||
                           desc.baseMipLevel >= texture->owned->mipCount() ||
                           desc.mipCount > texture->owned->mipCount() - desc.baseMipLevel)) {
        Log::fatal("VulkanDevice", "TextureView does not match its Texture");
    }
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = texture->handle();
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = toVulkan(desc.format);
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = desc.baseMipLevel;
    info.subresourceRange.levelCount = desc.mipCount;
    info.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    check(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView(texture)");
    return textureViews_.insert(TextureViewResource{view, true});
}

void VulkanDevice::destroyTextureView(TextureViewHandle handle) {
    TextureViewResource* resource = textureViews_.find(handle);
    if (!resource)
        return;
    if (resource->owned)
        vkDestroyImageView(device_, resource->resource, nullptr);
    (void)textureViews_.release(handle);
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc& desc) {
    if (desc.maxAnisotropy != 1.0F)
        Log::fatal("VulkanDevice", "Texture v1 does not support anisotropic sampling");
    return samplers_.insert(SamplerResource{std::make_unique<VulkanSampler>(device_, desc)});
}

void VulkanDevice::destroySampler(SamplerHandle handle) {
    (void)samplers_.release(handle);
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc& desc) {
    if (desc.bytecode.empty()) {
        Log::fatal("VulkanDevice", "Cannot create an empty shader");
    }
    return shaders_.insert(ShaderResource{
        std::make_unique<VulkanShaderModule>(device_, desc.stage, desc.bytecode, desc.debugName)});
}

void VulkanDevice::destroyShader(ShaderHandle handle) {
    (void)shaders_.release(handle);
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
    return pipelines_.insert(PipelineResource{
        std::make_unique<VulkanGraphicsPipeline>(device_,
                                                 desc,
                                                 resolveShader(desc.vertexShader),
                                                 resolveShader(desc.fragmentShader),
                                                 layouts)});
}

void VulkanDevice::destroyGraphicsPipeline(GraphicsPipelineHandle handle) {
    (void)pipelines_.release(handle);
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
    return bindGroupLayouts_.insert(
        BindGroupLayoutResource{std::make_unique<VulkanDescriptorSetLayout>(device_, bindings)});
}

void VulkanDevice::destroyBindGroupLayout(BindGroupLayoutHandle handle) {
    (void)bindGroupLayouts_.release(handle);
}

BindGroupHandle VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
    const VkDescriptorSet descriptor =
        descriptorAllocator_->allocate(resolveBindGroupLayout(desc.layout));
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    writes.reserve(desc.entries.size());
    for (const BindGroupEntry& entry : desc.entries) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = toVulkan(entry.type);
        if (entry.type == BindingType::SampledTexture) {
            imageInfos.push_back({resolveSampler(entry.sampler),
                                  resolveTextureView(entry.textureView),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            continue;
        }
        const VulkanBuffer& buffer = requireBuffer(entry.buffer);
        const std::uint64_t size = entry.size == 0 ? buffer.size() - entry.offset : entry.size;
        if (entry.offset > buffer.size() || size > buffer.size() - entry.offset) {
            Log::fatal("VulkanDevice", "Invalid bind group buffer range");
        }
        bufferInfos.push_back({buffer.handle(), entry.offset, size});
        write.pBufferInfo = &bufferInfos.back();
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(
        device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return bindGroups_.insert(BindGroupResource{descriptor});
}

void VulkanDevice::destroyBindGroup(BindGroupHandle handle) {
    BindGroupResource* resource = bindGroups_.find(handle);
    if (!resource)
        return;
    descriptorAllocator_->free(resource->resource);
    (void)bindGroups_.release(handle);
}

void VulkanDevice::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

VulkanBuffer& VulkanDevice::requireBuffer(BufferHandle handle) {
    return const_cast<VulkanBuffer&>(std::as_const(*this).requireBuffer(handle));
}

VulkanDevice::BufferResource& VulkanDevice::requireBufferResource(BufferHandle handle) {
    return const_cast<BufferResource&>(std::as_const(*this).requireBufferResource(handle));
}

const VulkanDevice::BufferResource& VulkanDevice::requireBufferResource(BufferHandle handle) const {
    const BufferResource* resource = buffers_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI buffer handle");
    }
    return *resource;
}

const VulkanBuffer& VulkanDevice::requireBuffer(BufferHandle handle) const {
    return *requireBufferResource(handle).resource;
}

VkBuffer VulkanDevice::resolveBuffer(BufferHandle handle) const {
    return requireBuffer(handle).handle();
}

VkImage VulkanDevice::resolveTexture(TextureHandle handle) const {
    const TextureResource* resource = textures_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI texture handle");
    }
    return resource->handle();
}

VkImageView VulkanDevice::resolveTextureView(TextureViewHandle handle) const {
    const TextureViewResource* resource = textureViews_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI texture view handle");
    }
    return resource->resource;
}

VkSampler VulkanDevice::resolveSampler(SamplerHandle handle) const {
    const SamplerResource* resource = samplers_.find(handle);
    if (!resource || !resource->resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI sampler handle");
    }
    return resource->resource->handle();
}

VkShaderModule VulkanDevice::resolveShader(ShaderHandle handle) const {
    const ShaderResource* resource = shaders_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI shader handle");
    }
    return resource->resource->handle();
}

VkDescriptorSetLayout VulkanDevice::resolveBindGroupLayout(BindGroupLayoutHandle handle) const {
    const BindGroupLayoutResource* resource = bindGroupLayouts_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI bind group layout handle");
    }
    return resource->resource->handle();
}

ResolvedPipeline VulkanDevice::resolvePipeline(GraphicsPipelineHandle handle) const {
    const PipelineResource* resource = pipelines_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI graphics pipeline handle");
    }
    return {resource->resource->handle(), resource->resource->layout()};
}

VkDescriptorSet VulkanDevice::resolveBindGroup(BindGroupHandle handle) const {
    const BindGroupResource* resource = bindGroups_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI bind group handle");
    }
    return resource->resource;
}

TextureHandle VulkanDevice::registerExternalTexture(VkImage image) {
    if (image == VK_NULL_HANDLE) {
        Log::fatal("VulkanDevice", "Cannot register a null Vulkan image");
    }
    return textures_.insert(TextureResource{nullptr, image, true});
}

void VulkanDevice::unregisterExternalTexture(TextureHandle handle) {
    (void)textures_.release(handle);
}

TextureViewHandle VulkanDevice::registerExternalTextureView(VkImageView view) {
    if (view == VK_NULL_HANDLE) {
        Log::fatal("VulkanDevice", "Cannot register a null Vulkan image view");
    }
    return textureViews_.insert(TextureViewResource{view, false});
}

void VulkanDevice::unregisterExternalTextureView(TextureViewHandle handle) {
    (void)textureViews_.release(handle);
}

void VulkanDevice::clear() {
    pipelines_.clear();
    bindGroups_.forEach([this](const BindGroupResource& resource) {
        descriptorAllocator_->free(resource.resource);
    });
    bindGroups_.clear();
    descriptorAllocator_.reset();
    bindGroupLayouts_.clear();
    shaders_.clear();
    samplers_.clear();
    textureViews_.forEach([this](const TextureViewResource& resource) {
        if (resource.owned)
            vkDestroyImageView(device_, resource.resource, nullptr);
    });
    textureViews_.clear();
    textures_.clear();
    buffers_.clear();
}

} // namespace engine::rhi::vulkan
