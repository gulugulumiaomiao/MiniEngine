#include "rhi/vulkan/VulkanDevice.h"

#include "core/filesystem/FileSystem.h"
#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanGraphicsPipeline.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"
#include "rhi/vulkan/VulkanSampler.h"
#include "rhi/vulkan/VulkanShaderModule.h"

#include <algorithm>
#include <numeric>
#include <array>
#include <cstring>
#include <limits>
#if defined(MINI_DEBUG)
#include <iostream>
#endif
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace engine::rhi::vulkan {
namespace {

#if defined(MINI_DEBUG)
constexpr std::array kValidationLayers{"VK_LAYER_KHRONOS_validation"};
#endif
constexpr std::array kDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                       VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME};
const VirtualPath kPipelineCachePath{"shader-cache://pipeline_cache.bin"};

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

std::vector<std::byte> loadPipelineCacheInitialData(const VirtualPath& path,
                                                    VkPhysicalDevice physicalDevice) {
    const auto data = FILE_SYSTEM.readBinary(path);
    if (!data)
        return {};
    if (data->size() < sizeof(VkPipelineCacheHeaderVersionOne))
        return {};
    const auto* header = reinterpret_cast<const VkPipelineCacheHeaderVersionOne*>(data->data());
    if (header->headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
        return {};
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    if (std::memcmp(header->pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return {};
    return *data;
}

} // namespace

VulkanDevice::VulkanDevice(const SurfaceSource& surface, bool enablePipelineCache) {
    createInstance();
    createDebugMessenger();
    createSurface(surface);
    selectPhysicalDevice();
    createLogicalDevice();
    createAllocator();
    createCommandPool();
    if (enablePipelineCache)
        createPipelineCache();
}

VulkanDevice::~VulkanDevice() {
    waitIdle();
    savePipelineCache();
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
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
    features13.pNext = &dynamicState3Features;
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    return formatCount > 0 && presentModeCount > 0 && features13.dynamicRendering &&
           dynamicState3Features.extendedDynamicState3ColorBlendEnable &&
           dynamicState3Features.extendedDynamicState3ColorBlendEquation &&
           dynamicState3Features.extendedDynamicState3ColorWriteMask &&
           dynamicState3Features.extendedDynamicState3PolygonMode;
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
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    maxSamplerAnisotropy_ = properties.limits.maxSamplerAnisotropy;
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
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT};
    dynamicState3Features.extendedDynamicState3ColorBlendEnable = VK_TRUE;
    dynamicState3Features.extendedDynamicState3ColorBlendEquation = VK_TRUE;
    dynamicState3Features.extendedDynamicState3ColorWriteMask = VK_TRUE;
    dynamicState3Features.extendedDynamicState3PolygonMode = VK_TRUE;
    features13.pNext = &dynamicState3Features;
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

RID VulkanDevice::buffer_allocate_rid(const BufferDesc& desc) {
    if (desc.size == 0 || desc.usage == BufferUsage::None) {
        Log::fatal("VulkanDevice", "Invalid buffer description");
    }
    return buffers_.emplace(allocator_,
                            static_cast<VkDeviceSize>(desc.size),
                            desc.usage,
                            desc.memoryUsage);
}

void VulkanDevice::buffer_allocate_memory(RID handle) {
    requireBuffer(handle).allocateMemory();
}

void VulkanDevice::buffer_free_memory(RID handle) {
    if (VulkanBuffer* buffer = buffers_.find(handle)) {
        buffer->freeMemory();
    }
}

void VulkanDevice::buffer_release_rid(RID handle) {
    (void)buffers_.release(handle);
}

RID VulkanDevice::buffer_create(const BufferDesc& desc) {
    const RID handle = buffer_allocate_rid(desc);
    buffer_allocate_memory(handle);
    return handle;
}

void VulkanDevice::buffer_destroy(RID handle) {
    buffer_free_memory(handle);
    buffer_release_rid(handle);
}

void VulkanDevice::buffer_upload(RID destination,
                                 std::span<const std::byte> data,
                                 std::uint64_t offset) {
    VulkanBuffer& target = requireBuffer(destination);
    if (data.empty() || offset > target.size() || data.size_bytes() > target.size() - offset) {
        Log::fatal("VulkanDevice", "Invalid buffer upload range");
    }

    if (target.memoryUsage() == MemoryUsage::Upload) {
        target.upload(data, offset);
        return;
    }

    const BufferDesc stagingDesc{
        data.size_bytes(), BufferUsage::TransferSource, MemoryUsage::Upload, "upload staging"};
    const RID staging = buffer_create(stagingDesc);
    requireBuffer(staging).upload(data);

    const RID command = command_buffer_create();
    VulkanCommandBuffer& cmd = command_buffer(command);
    cmd.begin();
    cmd.copyBuffer({staging, destination, 0, offset, data.size_bytes()});
    cmd.end();
    submit(command, SubmitSync{});
    waitIdle();

    command_buffer_destroy(command);
    buffer_destroy(staging);
}

TextureFormatCapabilities VulkanDevice::texture_format_capabilities(
    PixelFormat format, TextureTiling tiling) const {
    if (!formatInfo(format).valid() ||
        (tiling != TextureTiling::Optimal && tiling != TextureTiling::Linear)) return {};
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, toVulkan(format), &properties);
    const auto features = tiling == TextureTiling::Optimal ? properties.optimalTilingFeatures :
                                                           properties.linearTilingFeatures;
    return {(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0,
            (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0,
            (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0,
            (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0,
            (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0,
            (features & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT) != 0,
            (features & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) != 0};
}

bool VulkanDevice::texture_is_supported(const TextureDesc& desc) const {
    constexpr auto knownUsage = TextureUsage::Sampled | TextureUsage::TransferSource |
        TextureUsage::TransferDestination | TextureUsage::ColorAttachment |
        TextureUsage::DepthStencilAttachment | TextureUsage::InputAttachment;
    if (desc.dimension != TextureType::Texture2D || !formatInfo(desc.format).valid() ||
        desc.width == 0 || desc.height == 0 || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.mipCount == 0 || desc.mipCount > static_cast<std::uint32_t>(std::bit_width(std::max(desc.width, desc.height))) ||
        (desc.samples != SampleCount::One && desc.samples != SampleCount::Two &&
         desc.samples != SampleCount::Four && desc.samples != SampleCount::Eight) ||
        (desc.samples != SampleCount::One && desc.mipCount != 1) || desc.usage == TextureUsage::None ||
        (static_cast<std::uint32_t>(desc.usage) & ~static_cast<std::uint32_t>(knownUsage)) != 0 ||
        (desc.tiling != TextureTiling::Optimal && desc.tiling != TextureTiling::Linear)) return false;
    const auto caps = texture_format_capabilities(desc.format, desc.tiling);
    if ((hasFlag(desc.usage, TextureUsage::Sampled) && !caps.sampled) ||
        (hasFlag(desc.usage, TextureUsage::TransferSource) && !caps.transferSource) ||
        (hasFlag(desc.usage, TextureUsage::TransferDestination) && !caps.transferDestination) ||
        (hasFlag(desc.usage, TextureUsage::ColorAttachment) && !caps.colorAttachment) ||
        (hasFlag(desc.usage, TextureUsage::DepthStencilAttachment) && !caps.depthStencilAttachment) ||
        (hasFlag(desc.usage, TextureUsage::InputAttachment) &&
         !caps.colorAttachment && !caps.depthStencilAttachment)) return false;
    VkImageFormatProperties properties{};
    const auto result = vkGetPhysicalDeviceImageFormatProperties(physicalDevice_, toVulkan(desc.format),
        VK_IMAGE_TYPE_2D, desc.tiling == TextureTiling::Optimal ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR,
        toVulkan(desc.usage), 0, &properties);
    if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) return false;
    check(result, "vkGetPhysicalDeviceImageFormatProperties");
    return desc.width <= properties.maxExtent.width && desc.height <= properties.maxExtent.height &&
        desc.depth <= properties.maxExtent.depth && desc.mipCount <= properties.maxMipLevels &&
        desc.arrayLayers <= properties.maxArrayLayers && (properties.sampleCounts & toVulkan(desc.samples)) != 0;
}

RID VulkanDevice::texture_create(const TextureDesc& desc) {
    if (!texture_is_supported(desc)) {
        Log::error("VulkanDevice", "Invalid or unsupported texture description: " + desc.debugName);
        return {};
    }
    // No eager default view: texture_default_view() creates + caches it lazily at device level.
    const auto handle = textures_.emplace(allocator_, desc);
    if (!texture(handle).handle()) {
        (void)textures_.release(handle);
        return {};
    }
    return handle;
}

void VulkanDevice::texture_destroy(RID handle) {
    if (!textures_.find(handle))
        return;
    // Cascade-release every view deduped for this texture, then the texture itself.
    if (const auto it = viewDedup_.find(handle.value()); it != viewDedup_.end()) {
        for (const auto& [viewDesc, view] : it->second)
            texture_view_release_rid(view);
        viewDedup_.erase(it);
    }
    texture_release_rid(handle);
}

void VulkanDevice::texture_upload(RID destination,
                                  std::span<const TextureUploadRegion> regions) {
    VulkanTexture* resource = textures_.find(destination);
    if (!resource || resource->uploaded() || regions.empty())
        Log::fatal("VulkanDevice", "Invalid Texture upload");
    const VulkanTexture& image = *resource;
    const auto info = formatInfo(image.format());
    if (!info.valid() || info.stencil || image.samples() != SampleCount::One ||
        !hasFlag(image.usage(), TextureUsage::TransferDestination) ||
        !hasFlag(image.usage(), TextureUsage::Sampled))
        Log::fatal("VulkanDevice", "Texture upload requires a sampled single-sample transfer destination");
    if (regions.size() != image.mipCount())
        Log::fatal("VulkanDevice", "Texture upload must provide the complete mip chain");
    // Align each copy independently; CPU blobs remain tightly packed. Depth copies
    // require four-byte offsets, and color copies must respect their texel block size.
    TextureMipLayout stagingLayout;
    const auto alignment = std::lcm(std::size_t{4}, static_cast<std::size_t>(info.bytesPerBlock));
    if (!computeTextureLayout(image.format(), image.width(), image.height(), image.mipCount(),
                              stagingLayout, alignment))
        Log::fatal("VulkanDevice", "Invalid or overflowing texture upload layout");
    std::vector<bool> suppliedMips(image.mipCount());
    for (const TextureUploadRegion& region : regions) {
        if (region.data.empty() || region.mipLevel >= image.mipCount() || region.arrayLayer != 0 ||
            suppliedMips[region.mipLevel]) {
            Log::fatal("VulkanDevice", "Invalid Texture upload region");
        }
        const std::uint32_t expectedWidth = std::max(1U, image.width() >> region.mipLevel);
        const std::uint32_t expectedHeight = std::max(1U, image.height() >> region.mipLevel);
        if (region.width != expectedWidth || region.height != expectedHeight ||
            region.data.size_bytes() != stagingLayout.sizes[region.mipLevel]) {
            Log::fatal("VulkanDevice", "Texture upload region does not match the mip level");
        }
        suppliedMips[region.mipLevel] = true;
    }
    std::vector<BufferImageCopy> copies;
    copies.reserve(regions.size());
    RID staging, command;
    bool submitted = false;
    try {
        staging = buffer_allocate_rid({stagingLayout.totalSize, BufferUsage::TransferSource,
                                       MemoryUsage::Upload, "texture upload staging"});
        buffer_allocate_memory(staging);
        command = command_buffer_create();
        for (const TextureUploadRegion& region : regions) {
            const auto stagingOffset = stagingLayout.offsets[region.mipLevel];
            requireBuffer(staging).upload(region.data, stagingOffset);
            copies.push_back(BufferImageCopy{staging,
                                             destination,
                                             stagingOffset,
                                             0,
                                             0,
                                             region.mipLevel,
                                             region.arrayLayer,
                                             {},
                                             Extent3D{region.width, region.height, 1}});
        }

        VulkanCommandBuffer& cmd = command_buffer(command);
        cmd.begin();
        const auto aspect = isDepthFormat(image.format()) ? TextureAspect::Depth : TextureAspect::Color;
        const TextureBarrier toCopyDestination{destination,
                                               aspect,
                                               ResourceState::Undefined,
                                               ResourceState::CopyDestination,
                                               0,
                                               kRemainingMipLevels,
                                               0,
                                               1};
        cmd.resourceBarriers(std::span{&toCopyDestination, 1});
        for (const BufferImageCopy& copy : copies) {
            cmd.copyBufferToImage(copy);
        }
        const TextureBarrier toShaderRead{destination,
                                          aspect,
                                          ResourceState::CopyDestination,
                                          ResourceState::ShaderRead,
                                          0,
                                          kRemainingMipLevels,
                                          0,
                                          1};
        cmd.resourceBarriers(std::span{&toShaderRead, 1});
        cmd.end();
        submit(command, SubmitSync{});
        submitted = true;
        waitIdle();
    } catch (...) {
        if (submitted) waitIdle();
        if (command) command_buffer_destroy(command);
        if (staging) buffer_destroy(staging);
        throw;
    }
    command_buffer_destroy(command);
    buffer_destroy(staging);
    resource->markUploaded();
}

RID VulkanDevice::texture_view_create(RID textureHandle, const TextureViewDesc& desc) {
    VulkanTexture* texture = textures_.find(textureHandle);
    if (!texture)
        Log::fatal("VulkanDevice", "Invalid RHI Texture handle");
    const auto usage = texture->usage();
    if (!hasFlag(usage, TextureUsage::Sampled) && !hasFlag(usage, TextureUsage::ColorAttachment) &&
        !hasFlag(usage, TextureUsage::DepthStencilAttachment) && !hasFlag(usage, TextureUsage::InputAttachment)) {
        Log::error("VulkanDevice", "Transfer-only textures cannot have image views");
        return {};
    }
    if (desc.type != TextureType::Texture2D || desc.mipCount == 0 || desc.layerCount != 1 ||
        desc.baseLayer != 0) {
        Log::fatal("VulkanDevice", "Invalid or unsupported TextureView description");
    }
    TextureViewDesc normalized = desc;
    if (normalized.format == PixelFormat::Undefined)
        normalized.format = texture->format();
    if (normalized.format != texture->format() || normalized.baseMip >= texture->mipCount() ||
        normalized.mipCount > texture->mipCount() - normalized.baseMip) {
        Log::fatal("VulkanDevice", "TextureView does not match its Texture");
    }
    auto& views = viewDedup_[textureHandle.value()];
    if (const auto found = views.find(normalized);
        found != views.end() && textureViews_.find(found->second))
        return found->second;
    const RID view = textureViews_.emplace(device_, *texture, normalized);
    views.insert_or_assign(normalized, view);
    return view;
}

RID VulkanDevice::texture_default_view(RID texture) {
    const VulkanTexture* resource = textures_.find(texture);
    if (!resource)
        Log::fatal("VulkanDevice", "Invalid RHI Texture handle");
    const TextureViewDesc fullRange{.type = resource->type(),
                                    .format = resource->format(),
                                    .baseMip = 0,
                                    .mipCount = resource->mipCount(),
                                    .baseLayer = 0,
                                    .layerCount = resource->arrayLayers()};
    return texture_view_create(texture, fullRange);
}

void VulkanDevice::texture_view_destroy(RID handle) {
    if (!textureViews_.find(handle))
        return;
    for (auto& [textureKey, views] : viewDedup_) {
        for (auto it = views.begin(); it != views.end(); ++it) {
            if (it->second == handle) {
                views.erase(it);
                texture_view_release_rid(handle);
                return;
            }
        }
    }
    texture_view_release_rid(handle);
}

RID VulkanDevice::sampler_create(const SamplerDesc& desc) {
    SamplerDesc clamped = desc;
    clamped.maxAnisotropy = std::min(clamped.maxAnisotropy, maxSamplerAnisotropy_);
    if (clamped.maxAnisotropy < 1.0F)
        clamped.maxAnisotropy = 1.0F;
    // Identical descriptors share one VkSampler; only create on a cache miss.
    if (const auto found = samplerDedup_.find(clamped);
        found != samplerDedup_.end() && samplers_.find(found->second))
        return found->second;
    const RID handle = samplers_.emplace(device_, clamped, maxSamplerAnisotropy_);
    samplerDedup_.insert_or_assign(clamped, handle);
    return handle;
}

void VulkanDevice::sampler_destroy(RID handle) {
    if (!samplers_.find(handle))
        return;
    // Sampler no longer retains its desc; erase the dedup entry by matching the handle.
    for (auto it = samplerDedup_.begin(); it != samplerDedup_.end(); ++it) {
        if (it->second == handle) {
            samplerDedup_.erase(it);
            break;
        }
    }
    sampler_release_rid(handle);
}

RID VulkanDevice::shader_create(const ShaderDesc& desc) {
    if (desc.bytecode.empty()) {
        Log::fatal("VulkanDevice", "Cannot create an empty shader");
    }
    return shaders_.emplace(device_, desc.bytecode, desc.debugName);
}

void VulkanDevice::shader_destroy(RID handle) {
    shader_release_rid(handle);
}

RID VulkanDevice::pipeline_create(const GraphicsPipelineDesc& desc) {
    if (!desc.vertexShader || !desc.fragmentShader ||
        (desc.colorFormats.empty() && desc.depthFormat == PixelFormat::Undefined) ||
        std::ranges::any_of(desc.colorFormats,
                            [](PixelFormat format) { return !isColorFormat(format); }) ||
        (desc.depthFormat != PixelFormat::Undefined && !isDepthFormat(desc.depthFormat))) {
        Log::fatal("VulkanDevice", "Invalid graphics pipeline description");
    }
    for (const auto format : desc.colorFormats) {
        if (!attachmentSupported(format, desc.samples)) {
            Log::error("VulkanDevice", "Unsupported pipeline color format or sample count");
            return {};
        }
    }
    if (desc.depthFormat != PixelFormat::Undefined && !attachmentSupported(desc.depthFormat, desc.samples)) {
        Log::error("VulkanDevice", "Unsupported pipeline depth format or sample count");
        return {};
    }
    VkRenderPass nativeRenderPass = VK_NULL_HANDLE;
    if (desc.renderPass) {
        const auto& pass = renderPass(desc.renderPass);
        if (desc.subpass >= pass.desc().subpasses.size())
            Log::fatal("VulkanDevice", "Pipeline subpass out of range");
        const auto& subpass = pass.desc().subpasses[desc.subpass];
        if (desc.colorFormats.size() != subpass.colors.size())
            Log::fatal("VulkanDevice", "Pipeline color count does not match subpass");
        for (std::size_t i = 0; i < subpass.colors.size(); ++i) {
            const auto& a = pass.desc().attachments[subpass.colors[i].attachment];
            if (desc.colorFormats[i] != a.format || desc.samples != a.samples)
                Log::fatal("VulkanDevice", "Pipeline color format/samples do not match subpass");
        }
        const auto depth = subpass.depth.attachment;
        if ((depth == kUnusedAttachment && desc.depthFormat != PixelFormat::Undefined) ||
            (depth != kUnusedAttachment &&
             (desc.depthFormat != pass.desc().attachments[depth].format ||
              desc.samples != pass.desc().attachments[depth].samples)))
            Log::fatal("VulkanDevice", "Pipeline depth format/samples do not match subpass");
        nativeRenderPass = pass.handle();
    } else if (desc.subpass != 0) {
        Log::fatal("VulkanDevice", "Dynamic rendering has no subpass index");
    }
    const VkPipelineLayout layout = resolvePipelineLayout(desc.pipelineLayout);
    return pipelines_.emplace(device_,
                              desc,
                              resolveShader(desc.vertexShader),
                              resolveShader(desc.fragmentShader),
                              layout,
                              pipelineCache_, nativeRenderPass);
}

void VulkanDevice::pipeline_destroy(RID handle) {
    pipeline_release_rid(handle);
}

bool VulkanDevice::attachmentSupported(PixelFormat format, SampleCount samples) const {
    if (!isColorFormat(format) && !isDepthFormat(format)) return false;
    TextureDesc desc{.format = format, .width = 1, .height = 1,
        .usage = isDepthFormat(format) ? TextureUsage::DepthStencilAttachment : TextureUsage::ColorAttachment,
        .samples = samples};
    if (texture_is_supported(desc)) return true;
    desc.tiling = TextureTiling::Linear;
    return texture_is_supported(desc);
}

RID VulkanDevice::render_pass_create(const RenderPassDesc& desc) {
    for (const auto& attachment : desc.attachments) {
        if (!attachmentSupported(attachment.format, attachment.samples)) {
            Log::error("VulkanDevice", "Unsupported render pass attachment format or sample count");
            return {};
        }
    }
    return renderPasses_.emplace(device_, desc);
}

void VulkanDevice::render_pass_destroy(RID handle) {
    (void)renderPasses_.release(handle);
}

const VulkanRenderPass& VulkanDevice::renderPass(RID handle) const {
    const auto* pass = renderPasses_.find(handle);
    if (!pass) Log::fatal("VulkanDevice", "Invalid render pass handle");
    return *pass;
}

const VulkanFramebuffer& VulkanDevice::framebuffer(RID handle) const {
    const auto* result = framebuffers_.find(handle);
    if (!result) Log::fatal("VulkanDevice", "Invalid framebuffer handle");
    return *result;
}

TextureViewInfo VulkanDevice::texture_view_info(RID view) const {
    if (!textureViews_.find(view)) Log::fatal("VulkanDevice", "Invalid texture view handle");
    for (const auto& [textureKey, views] : viewDedup_) {
        for (const auto& [desc, handle] : views) {
            if (handle != view) continue;
            const RID textureHandle{static_cast<std::uint32_t>(textureKey),
                                    static_cast<std::uint32_t>(textureKey >> 32U)};
            const auto* texture = textures_.find(textureHandle);
            if (!texture) Log::fatal("VulkanDevice", "View has no live texture");
            return {textureHandle, desc.format, texture->samples(), texture->usage(),
                    std::max(1U, texture->width() >> desc.baseMip),
                    std::max(1U, texture->height() >> desc.baseMip), desc.baseMip,
                    desc.mipCount, desc.baseLayer, desc.layerCount};
        }
    }
    Log::fatal("VulkanDevice", "View metadata not found");
}

RID VulkanDevice::framebuffer_create(const FramebufferDesc& desc) {
    const auto& pass = renderPass(desc.renderPass);
    if (desc.width == 0 || desc.height == 0 || desc.layers != 1 ||
        desc.views.size() != pass.desc().attachments.size())
        Log::fatal("VulkanDevice", "Invalid framebuffer description");
    std::vector<VkImageView> views;
    for (std::size_t i = 0; i < desc.views.size(); ++i) {
        const auto info = texture_view_info(desc.views[i]);
        const auto& attachment = pass.desc().attachments[i];
        if (info.format != attachment.format || info.samples != attachment.samples ||
            info.width < desc.width || info.height < desc.height || info.mipCount != 1 ||
            info.layerCount < desc.layers)
            Log::fatal("VulkanDevice", "Framebuffer view is incompatible with render pass");
        views.push_back(resolveTextureView(desc.views[i]));
    }
    return framebuffers_.emplace(device_, desc, pass.handle(), views);
}

void VulkanDevice::framebuffer_destroy(RID handle) {
    (void)framebuffers_.release(handle);
}

// ---------------------------------------------------------------------------
// New uniform-binding system: shader RID primitives, descriptor set layouts,
// pipeline layouts and bucketed descriptor sets.
// ---------------------------------------------------------------------------

namespace {

// Per-type descriptor counts for a single set, sorted by VkDescriptorType so two profiles can be
// compared for equality regardless of the binding order they were reflected in.
[[nodiscard]] std::vector<VkDescriptorPoolSize>
descriptorProfile(std::span<const ResourceBindingDesc> bindings) {
    std::vector<VkDescriptorPoolSize> profile;
    for (const ResourceBindingDesc& binding : bindings) {
        const VkDescriptorType type = toVulkan(binding.type);
        const auto found = std::ranges::find_if(profile, [type](const VkDescriptorPoolSize& size) {
            return size.type == type;
        });
        if (found != profile.end())
            ++found->descriptorCount;
        else
            profile.push_back({type, 1});
    }
    std::ranges::sort(profile, [](const VkDescriptorPoolSize& a, const VkDescriptorPoolSize& b) {
        return a.type < b.type;
    });
    return profile;
}

[[nodiscard]] bool sameDescriptorProfile(const std::vector<VkDescriptorPoolSize>& a,
                                         const std::vector<VkDescriptorPoolSize>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].type != b[i].type || a[i].descriptorCount != b[i].descriptorCount)
            return false;
    return true;
}

} // namespace

RID VulkanDevice::shader_allocate_rid() {
    return shaders_.emplace();
}

void VulkanDevice::shader_release_rid(RID handle) {
    (void)shaders_.release(handle);
}

RID VulkanDevice::descriptor_set_layout_allocate_rid() {
    return setLayouts_.emplace();
}

void VulkanDevice::descriptor_set_layout_build(RID handle, const DescriptorSetLayoutDesc& desc) {
    VulkanDescriptorSetLayout* layout = setLayouts_.find(handle);
    if (!layout)
        Log::fatal("VulkanDevice", "Invalid descriptor set layout handle");
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());
    for (const ResourceBindingDesc& binding : desc.bindings) {
        bindings.push_back({binding.binding,
                            toVulkan(binding.type),
                            1,
                            binding.type == ResourceType::InputAttachment
                                ? VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT}
                                : VkShaderStageFlags{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
                            nullptr});
    }
    layout->initialize(device_, bindings);
}

void VulkanDevice::descriptor_set_layout_destroy(RID handle) {
    set_layout_release_rid(handle);
}

RID VulkanDevice::pipeline_layout_create(const PipelineLayoutDesc& desc) {
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(desc.setLayouts.size());
    for (RID handle : desc.setLayouts)
        layouts.push_back(resolveDescriptorSetLayout(handle));
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &info, nullptr, &layout) != VK_SUCCESS)
        Log::fatal("VulkanDevice", "vkCreatePipelineLayout failed");
    return pipelineLayoutPool_.emplace(layout);
}

void VulkanDevice::pipeline_layout_destroy(RID handle) {
    VkPipelineLayout* layout = pipelineLayoutPool_.find(handle);
    if (!layout)
        return;
    vkDestroyPipelineLayout(device_, *layout, nullptr);
    (void)pipelineLayoutPool_.release(handle);
}

RID VulkanDevice::descriptor_set_allocate(const DescriptorSetAllocateInfo& info) {
    if (info.setCount != 1)
        Log::fatal("VulkanDevice", "descriptor_set_allocate supports exactly one set");
    const VkDescriptorSetLayout vkLayout = resolveDescriptorSetLayout(info.layout);
    const std::vector<VkDescriptorPoolSize> profile = descriptorProfile(info.bindings);

    // Pick the first pool in this hash bucket whose profile matches and that still has room; the
    // profile match guarantees per-type capacity (sized as profile * maxSets) is never exceeded.
    DescriptorPoolBucket& bucket = descriptorPools_[info.unorderedHash];
    VulkanDescriptorPool* pool = nullptr;
    for (VulkanDescriptorPool& candidate : bucket.pools) {
        if (candidate.allocatedSets < candidate.maxSets &&
            sameDescriptorProfile(candidate.perSetProfile, profile)) {
            pool = &candidate;
            break;
        }
    }
    if (!pool) {
        VulkanDescriptorPool fresh;
        fresh.id = bucket.nextId++;
        fresh.maxSets = kDescriptorSetsPerPool;
        fresh.perSetProfile = profile;
        std::vector<VkDescriptorPoolSize> sizes;
        sizes.reserve(profile.size());
        for (const VkDescriptorPoolSize& entry : profile)
            sizes.push_back({entry.type, entry.descriptorCount * fresh.maxSets});
        VkDescriptorPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        createInfo.maxSets = fresh.maxSets;
        createInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        createInfo.pPoolSizes = sizes.data();
        if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &fresh.pool) != VK_SUCCESS)
            Log::fatal("VulkanDevice", "vkCreateDescriptorPool failed");
        bucket.pools.push_back(std::move(fresh));
        pool = &bucket.pools.back();
    }

    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = pool->pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &vkLayout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_, &allocateInfo, &set) != VK_SUCCESS)
        Log::fatal("VulkanDevice", "vkAllocateDescriptorSets failed");
    ++pool->allocatedSets;
    return descriptorSets_.emplace(VulkanDescriptorSet{set, info.unorderedHash, pool->id});
}

void VulkanDevice::descriptor_set_update(RID handle, std::span<const DescriptorWrite> writes) {
    const VulkanDescriptorSet* entry = descriptorSets_.find(handle);
    if (!entry)
        Log::fatal("VulkanDevice", "Invalid descriptor set handle");
    // Reserve up front so the info vectors never reallocate and invalidate the p*Info pointers
    // stored in each VkWriteDescriptorSet below.
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> vkWrites;
    bufferInfos.reserve(writes.size());
    imageInfos.reserve(writes.size());
    vkWrites.reserve(writes.size());
    for (const DescriptorWrite& write : writes) {
        VkWriteDescriptorSet vkWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        vkWrite.dstSet = entry->set;
        vkWrite.dstBinding = write.binding;
        vkWrite.descriptorCount = 1;
        vkWrite.descriptorType = toVulkan(write.type);
        switch (write.type) {
        case ResourceType::UniformBuffer:
        case ResourceType::StorageBuffer: {
            const VulkanBuffer& buffer = requireBuffer(write.buffer);
            const std::uint64_t size = write.size == 0 ? buffer.size() - write.offset : write.size;
            bufferInfos.push_back({buffer.handle(), write.offset, size});
            vkWrite.pBufferInfo = &bufferInfos.back();
            break;
        }
        case ResourceType::CombinedImageSampler:
            imageInfos.push_back({resolveSampler(write.sampler),
                                  resolveTextureView(write.textureView),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            vkWrite.pImageInfo = &imageInfos.back();
            break;
        case ResourceType::SampledImage:
        case ResourceType::InputAttachment:
            imageInfos.push_back({VK_NULL_HANDLE,
                                  resolveTextureView(write.textureView),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            vkWrite.pImageInfo = &imageInfos.back();
            break;
        case ResourceType::Sampler:
            imageInfos.push_back(
                {resolveSampler(write.sampler), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
            vkWrite.pImageInfo = &imageInfos.back();
            break;
        case ResourceType::StorageImage:
            imageInfos.push_back(
                {VK_NULL_HANDLE, resolveTextureView(write.textureView), VK_IMAGE_LAYOUT_GENERAL});
            vkWrite.pImageInfo = &imageInfos.back();
            break;
        }
        vkWrites.push_back(vkWrite);
    }
    vkUpdateDescriptorSets(
        device_, static_cast<std::uint32_t>(vkWrites.size()), vkWrites.data(), 0, nullptr);
}

void VulkanDevice::descriptor_set_destroy(RID handle) {
    const VulkanDescriptorSet* entry = descriptorSets_.find(handle);
    if (!entry)
        return;
    // Copy the locator fields out before release() recycles the slot.
    const VkDescriptorSet set = entry->set;
    const std::uint64_t hash = entry->unorderedHash;
    const std::uint32_t poolId = entry->poolId;
    if (const auto bucketIt = descriptorPools_.find(hash); bucketIt != descriptorPools_.end()) {
        DescriptorPoolBucket& bucket = bucketIt->second;
        const auto poolIt =
            std::ranges::find_if(bucket.pools,
                                 [poolId](const VulkanDescriptorPool& candidate) {
                                     return candidate.id == poolId;
                                 });
        if (poolIt != bucket.pools.end()) {
            vkFreeDescriptorSets(device_, poolIt->pool, 1, &set);
            if (poolIt->allocatedSets > 0)
                --poolIt->allocatedSets;
            // A pool with no live sets is destroyed and dropped from the bucket.
            if (poolIt->allocatedSets == 0) {
                vkDestroyDescriptorPool(device_, poolIt->pool, nullptr);
                bucket.pools.erase(poolIt);
            }
        }
        // An emptied bucket is removed from the map entirely.
        if (bucket.pools.empty())
            descriptorPools_.erase(bucketIt);
    }
    (void)descriptorSets_.release(handle);
}

VkDescriptorSetLayout VulkanDevice::resolveDescriptorSetLayout(RID handle) const {
    const VulkanDescriptorSetLayout* resource = setLayouts_.find(handle);
    if (!resource)
        Log::fatal("VulkanDevice", "Invalid or stale RHI descriptor set layout handle");
    return resource->handle();
}

VkPipelineLayout VulkanDevice::resolvePipelineLayout(RID handle) const {
    const VkPipelineLayout* layout = pipelineLayoutPool_.find(handle);
    if (!layout)
        Log::fatal("VulkanDevice", "Invalid or stale RHI pipeline layout handle");
    return *layout;
}

VkDescriptorSet VulkanDevice::resolveDescriptorSet(RID handle) const {
    const VulkanDescriptorSet* entry = descriptorSets_.find(handle);
    if (!entry)
        Log::fatal("VulkanDevice", "Invalid or stale RHI descriptor set handle");
    return entry->set;
}

void VulkanDevice::destroyDescriptorPools() {
    for (auto& [hash, bucket] : descriptorPools_)
        for (VulkanDescriptorPool& pool : bucket.pools)
            if (pool.pool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(device_, pool.pool, nullptr);
    descriptorPools_.clear();
}

RID VulkanDevice::command_buffer_create() {
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
          "vkAllocateCommandBuffers");
    return commandBuffers_.emplace(commandBuffer, *this, /*owned=*/true);
}

void VulkanDevice::command_buffer_destroy(RID handle) {
    command_buffer_release_rid(handle);
}

RID VulkanDevice::command_buffer_allocate_rid(VkCommandBuffer native, bool owned) {
    return commandBuffers_.emplace(native, *this, owned);
}

void VulkanDevice::command_buffer_release_rid(RID handle) {
    (void)commandBuffers_.release(handle);
}

VulkanCommandBuffer& VulkanDevice::command_buffer(RID handle) {
    VulkanCommandBuffer* command = commandBuffers_.find(handle);
    if (!command) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI command buffer handle");
    }
    return *command;
}

void VulkanDevice::submit(RID command, const SubmitSync& sync) {
    VulkanCommandBuffer& recorded = command_buffer(command);
    if (recorded.state() != CommandState::Executable) {
        Log::fatal("VulkanDevice",
                   "submit requires a command buffer that finished recording (call end() first)");
    }
    VkCommandBuffer nativeCommandBuffer = recorded.nativeCommandBuffer();
    VkSemaphore waitSemaphore = sync.waitSemaphore;
    VkSemaphore signalSemaphore = sync.signalSemaphore;
    VkPipelineStageFlags waitStage = sync.waitStage;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = waitSemaphore != VK_NULL_HANDLE ? 1U : 0U;
    submitInfo.pWaitSemaphores = waitSemaphore != VK_NULL_HANDLE ? &waitSemaphore : nullptr;
    // Ignored by Vulkan when waitSemaphoreCount is zero.
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = signalSemaphore != VK_NULL_HANDLE ? 1U : 0U;
    submitInfo.pSignalSemaphores = signalSemaphore != VK_NULL_HANDLE ? &signalSemaphore : nullptr;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &nativeCommandBuffer;
    check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, sync.signalFence), "vkQueueSubmit");
}

void VulkanDevice::waitIdle() {
    if (device_ != VK_NULL_HANDLE) {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

RID VulkanDevice::buffer_acquire_transient(const BufferDesc& desc) {
    // A transient buffer is a normal host-visible buffer whose lifetime the device owns: it is
    // registered here, tagged with the submitting frame's fence at submit, and destroyed by
    // collectStagingBuffers() once that fence signals. Callers never free it (orphan semantics).
    const RID handle = buffer_create(desc);
    pendingStagingBuffers_.push_back(StagingBuffer{handle, VK_NULL_HANDLE});
    return handle;
}

RID VulkanDevice::acquireStagingBuffer(std::uint64_t size) {
    if (size == 0) {
        Log::fatal("VulkanDevice", "Staging buffer size must be positive");
    }
    return buffer_acquire_transient(
        {size, BufferUsage::TransferSource, MemoryUsage::Upload, "command buffer staging"});
}

void VulkanDevice::tagPendingStagingBuffers(VkFence fence) {
    for (StagingBuffer& staging : pendingStagingBuffers_) {
        if (staging.fence == VK_NULL_HANDLE) {
            staging.fence = fence;
        }
    }
}

void VulkanDevice::collectStagingBuffers() {
    retireStagingBuffers(pendingStagingBuffers_);
}

void VulkanDevice::retireStagingBuffers(std::vector<StagingBuffer>& pending) {
    for (auto entry = pending.begin(); entry != pending.end();) {
        // Untagged entries belong to a command buffer that has not been submitted yet;
        // leave them to the device teardown (which waits idle first).
        if (entry->fence != VK_NULL_HANDLE &&
            vkGetFenceStatus(device_, entry->fence) == VK_SUCCESS) {
            buffer_destroy(entry->handle);
            entry = pending.erase(entry);
            continue;
        }
        ++entry;
    }
}

VulkanBuffer& VulkanDevice::requireBuffer(RID handle) {
    return const_cast<VulkanBuffer&>(std::as_const(*this).requireBuffer(handle));
}

const VulkanBuffer& VulkanDevice::requireBuffer(RID handle) const {
    const VulkanBuffer* buffer = buffers_.find(handle);
    if (!buffer) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI buffer handle");
    }
    return *buffer;
}

VkBuffer VulkanDevice::resolveBuffer(RID handle) const {
    return requireBuffer(handle).handle();
}

VkImage VulkanDevice::resolveTexture(RID handle) const {
    return texture(handle).handle();
}

const VulkanTexture& VulkanDevice::texture(RID handle) const {
    const VulkanTexture* resource = textures_.find(handle);
    if (!resource)
        Log::fatal("VulkanDevice", "Invalid or stale RHI texture handle");
    return *resource;
}

PixelFormat VulkanDevice::textureFormat(RID handle) const {
    return texture(handle).format();
}

VkImageView VulkanDevice::resolveTextureView(RID handle) const {
    const VulkanTextureView* resource = textureViews_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI texture view handle");
    }
    return resource->handle();
}

VkSampler VulkanDevice::resolveSampler(RID handle) const {
    const VulkanSampler* resource = samplers_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI sampler handle");
    }
    return resource->handle();
}

VkShaderModule VulkanDevice::resolveShader(RID handle) const {
    const VulkanShaderModule* resource = shaders_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI shader handle");
    }
    return resource->handle();
}

ResolvedPipeline VulkanDevice::resolvePipeline(RID handle) const {
    const VulkanGraphicsPipeline* resource = pipelines_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI graphics pipeline handle");
    }
    return {resource->handle(), resource->layout(), resource->renderPass(), resource->subpass(),
            resource->colorAttachmentCount()};
}

RID VulkanDevice::texture_allocate_rid(VkImage image, const TextureDesc& desc) {
    return textures_.emplace(image, desc);
}

RID VulkanDevice::texture_view_allocate_rid(RID textureHandle,
                                            VkImageView view,
                                            const TextureViewDesc& desc) {
    VulkanTexture* texture = textures_.find(textureHandle);
    if (!texture)
        Log::fatal("VulkanDevice", "Cannot register a view for an invalid external texture");
    TextureViewDesc normalized = desc;
    if (normalized.format == PixelFormat::Undefined)
        normalized.format = texture->format();
    const RID handle = textureViews_.emplace(device_, *texture, view, normalized);
    // Device-level dedup/ownership (replaces the removed per-texture view cache + default view);
    // texture_default_view() will find this entry via the same normalized full-range desc.
    viewDedup_[textureHandle.value()].insert_or_assign(normalized, handle);
    return handle;
}

void VulkanDevice::createPipelineCache() {
    const auto physicalPath = FILE_SYSTEM.resolvePhysicalPath(kPipelineCachePath);
    if (physicalPath) {
        Log::info("VulkanDevice",
                  "Loading pipeline cache from %s -> %s",
                  kPipelineCachePath.string().c_str(),
                  physicalPath->string().c_str());
    } else {
        Log::warn("VulkanDevice",
                  "Cannot resolve pipeline cache virtual path: %s",
                  kPipelineCachePath.string().c_str());
    }
    const auto initialData = loadPipelineCacheInitialData(kPipelineCachePath, physicalDevice_);

    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    if (!initialData.empty()) {
        info.initialDataSize = initialData.size();
        info.pInitialData = initialData.data();
        if (vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_) == VK_SUCCESS)
            return;
        Log::warn("VulkanDevice",
                  "Discarding invalid pipeline cache data: %s",
                  kPipelineCachePath.string().c_str());
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
    }
    check(vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_), "vkCreatePipelineCache");
}

void VulkanDevice::savePipelineCache() {
    if (pipelineCache_ == VK_NULL_HANDLE)
        return;
    std::size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS ||
        size == 0) {
        vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
        return;
    }
    std::vector<std::byte> data(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) != VK_SUCCESS) {
        vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
        return;
    }
    (void)FILE_SYSTEM.createDirectories(kPipelineCachePath.parent());
    if (!FILE_SYSTEM.writeBinaryAtomic(kPipelineCachePath, data)) {
        Log::warn("VulkanDevice",
                  "Cannot persist the pipeline cache to %s",
                  kPipelineCachePath.string().c_str());
    }
    vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    pipelineCache_ = VK_NULL_HANDLE;
}

void VulkanDevice::clear() {
    // Command buffers only reference the device and command pool (both still alive here), so
    // clear them first; owned wrappers free their VkCommandBuffer back to the pool.
    commandBuffers_.clear();
    pipelines_.clear();
    framebuffers_.clear();
    renderPasses_.clear();
    // New uniform-binding system: descriptor sets are freed implicitly when their pools are
    // destroyed; pipeline layouts must be destroyed before the set layouts they reference.
    descriptorSets_.clear();
    destroyDescriptorPools();
    pipelineLayoutPool_.forEach(
        [this](VkPipelineLayout layout) { vkDestroyPipelineLayout(device_, layout, nullptr); });
    pipelineLayoutPool_.clear();
    setLayouts_.clear();
    shaders_.clear();
    samplerDedup_.clear();
    samplers_.clear();
    viewDedup_.clear();
    textureViews_.clear();
    textures_.clear();
    buffers_.clear();
}

} // namespace engine::rhi::vulkan
