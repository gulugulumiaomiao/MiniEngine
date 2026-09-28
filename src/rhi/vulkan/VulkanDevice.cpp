#include "rhi/vulkan/VulkanDevice.h"

#include "core/filesystem/FileSystem.h"
#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanDescriptorAllocator.h"
#include "rhi/vulkan/VulkanGraphicsPipeline.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/vulkan/VulkanTextureView.h"
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
    descriptorAllocator_ = std::make_unique<VulkanDescriptorAllocator>(device_, 256);
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

RID VulkanDevice::texture_create(const TextureDesc& desc) {
    if (desc.dimension != TextureType::Texture2D || desc.format == PixelFormat::Undefined ||
        desc.width == 0 || desc.height == 0 || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.mipCount == 0 || desc.usage == TextureUsage::None) {
        Log::fatal("VulkanDevice", "Invalid or unsupported Texture description");
    }
    if ((isColorFormat(desc.format) && hasFlag(desc.usage, TextureUsage::DepthStencilAttachment)) ||
        (isDepthFormat(desc.format) && hasFlag(desc.usage, TextureUsage::ColorAttachment))) {
        Log::fatal("VulkanDevice", "Texture format and attachment usage do not match");
    }
    // No eager default view: texture_default_view() creates + caches it lazily at device level.
    return textures_.emplace(allocator_, desc);
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
    const BufferDesc stagingDesc{
        totalSize, BufferUsage::TransferSource, MemoryUsage::Upload, "texture upload staging"};
    const RID staging = buffer_create(stagingDesc);
    std::uint64_t stagingOffset{};
    std::vector<BufferImageCopy> copies;
    copies.reserve(regions.size());
    for (const TextureUploadRegion& region : regions) {
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
        stagingOffset += region.data.size_bytes();
    }

    const RID command = command_buffer_create();
    VulkanCommandBuffer& cmd = command_buffer(command);
    cmd.begin();
    const TextureBarrier toCopyDestination{destination,
                                           TextureAspect::Color,
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
                                      TextureAspect::Color,
                                      ResourceState::CopyDestination,
                                      ResourceState::ShaderRead,
                                      0,
                                      kRemainingMipLevels,
                                      0,
                                      1};
    cmd.resourceBarriers(std::span{&toShaderRead, 1});
    cmd.end();
    submit(command, SubmitSync{});
    waitIdle();

    command_buffer_destroy(command);
    buffer_destroy(staging);
    resource->markUploaded();
}

RID VulkanDevice::texture_view_create(RID textureHandle, const TextureViewDesc& desc) {
    VulkanTexture* texture = textures_.find(textureHandle);
    if (!texture)
        Log::fatal("VulkanDevice", "Invalid RHI Texture handle");
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
    const PipelineLayoutKey key(desc.bindGroupLayouts.begin(), desc.bindGroupLayouts.end());
    return pipelines_.emplace(device_,
                              desc,
                              resolveShader(desc.vertexShader),
                              resolveShader(desc.fragmentShader),
                              acquirePipelineLayout(key),
                              pipelineCache_);
}

void VulkanDevice::pipeline_destroy(RID handle) {
    pipeline_release_rid(handle);
}

RID VulkanDevice::bind_group_layout_create(const BindGroupLayoutDesc& desc) {
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
    return bindGroupLayouts_.emplace(device_,
                                     std::span<const VkDescriptorSetLayoutBinding>{bindings});
}

void VulkanDevice::bind_group_layout_destroy(RID handle) {
    // Pipelines must be destroyed before the layouts they reference; drop the
    // cached pipeline layouts that still point at this descriptor set layout.
    destroyPipelineLayoutsReferencing(handle);
    bind_group_layout_release_rid(handle);
}

RID VulkanDevice::bind_group_create(const BindGroupDesc& desc) {
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
    return bindGroups_.emplace(descriptor);
}

void VulkanDevice::bind_group_destroy(RID handle) {
    VkDescriptorSet* descriptor = bindGroups_.find(handle);
    if (!descriptor)
        return;
    descriptorAllocator_->free(*descriptor);
    bind_group_release_rid(handle);
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
    const VulkanTexture* resource = textures_.find(handle);
    if (!resource)
        Log::fatal("VulkanDevice", "Invalid or stale RHI texture handle");
    return resource->handle();
}

PixelFormat VulkanDevice::textureFormat(RID handle) const {
    const VulkanTexture* resource = textures_.find(handle);
    if (!resource)
        Log::fatal("VulkanDevice", "RHI texture handle has no tracked format");
    return resource->format();
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

VkDescriptorSetLayout VulkanDevice::resolveBindGroupLayout(RID handle) const {
    const VulkanDescriptorSetLayout* resource = bindGroupLayouts_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI bind group layout handle");
    }
    return resource->handle();
}

ResolvedPipeline VulkanDevice::resolvePipeline(RID handle) const {
    const VulkanGraphicsPipeline* resource = pipelines_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI graphics pipeline handle");
    }
    return {resource->handle(), resource->layout()};
}

VkDescriptorSet VulkanDevice::resolveBindGroup(RID handle) const {
    const VkDescriptorSet* descriptor = bindGroups_.find(handle);
    if (!descriptor) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI bind group handle");
    }
    return *descriptor;
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

VkPipelineLayout VulkanDevice::acquirePipelineLayout(const PipelineLayoutKey& key) {
    if (const auto found = pipelineLayouts_.find(key); found != pipelineLayouts_.end())
        return found->second;
    std::vector<VkDescriptorSetLayout> layouts;
    layouts.reserve(key.size());
    for (RID handle : key)
        layouts.push_back(resolveBindGroupLayout(handle));
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<std::uint32_t>(layouts.size());
    info.pSetLayouts = layouts.data();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &info, nullptr, &layout) != VK_SUCCESS) {
        Log::fatal("VulkanDevice", "vkCreatePipelineLayout failed");
    }
    pipelineLayouts_.emplace(key, layout);
    return layout;
}

void VulkanDevice::destroyPipelineLayoutsReferencing(RID handle) {
    for (auto entry = pipelineLayouts_.begin(); entry != pipelineLayouts_.end();) {
        if (std::find(entry->first.begin(), entry->first.end(), handle) != entry->first.end()) {
            vkDestroyPipelineLayout(device_, entry->second, nullptr);
            entry = pipelineLayouts_.erase(entry);
        } else {
            ++entry;
        }
    }
}

void VulkanDevice::clear() {
    // Command buffers only reference the device and command pool (both still alive here), so
    // clear them first; owned wrappers free their VkCommandBuffer back to the pool.
    commandBuffers_.clear();
    pipelines_.clear();
    for (auto& [key, layout] : pipelineLayouts_)
        vkDestroyPipelineLayout(device_, layout, nullptr);
    pipelineLayouts_.clear();
    bindGroups_.forEach([this](VkDescriptorSet descriptor) {
        descriptorAllocator_->free(descriptor);
    });
    bindGroups_.clear();
    descriptorAllocator_.reset();
    bindGroupLayouts_.clear();
    shaders_.clear();
    samplerDedup_.clear();
    samplers_.clear();
    viewDedup_.clear();
    textureViews_.clear();
    textures_.clear();
    buffers_.clear();
}

} // namespace engine::rhi::vulkan
