#include "rhi/vulkan/VulkanDevice.h"

#include "core/filesystem/FileSystem.h"
#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanBuffer.h"
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

VkFormat toVulkan(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case PixelFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case PixelFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case PixelFormat::Undefined: break;
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
    if (hasFlag(usage, TextureUsage::ColorAttachment))
        result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasFlag(usage, TextureUsage::DepthStencilAttachment))
        result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

VmaAllocator VulkanDevice::allocator() const {
    return allocator_;
}

RID VulkanDevice::createBuffer(const BufferDesc& desc) {
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

void VulkanDevice::destroyBuffer(RID handle) {
    (void)buffers_.release(handle);
}

void VulkanDevice::uploadBuffer(RID destination,
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

    const BufferDesc stagingDesc{
        data.size_bytes(), BufferUsage::TransferSource, MemoryUsage::Upload, "upload staging"};
    const RID staging = createBuffer(stagingDesc);
    requireBuffer(staging).upload(data);

    std::unique_ptr<ICommandBuffer> command = createCommandBuffer();
    command->begin();
    command->copyBuffer({staging, destination, 0, offset, data.size_bytes()});
    command->end();
    submitCommand(*command, SubmitSync{});
    waitIdle();

    destroyBuffer(staging);
    // command is freed after waitIdle, so its VkCommandBuffer is no longer in use.
}

RID VulkanDevice::createTexture(const TextureDesc& desc) {
    if (desc.dimension != TextureType::Texture2D || desc.format == PixelFormat::Undefined ||
        desc.width == 0 || desc.height == 0 || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.mipCount == 0 || desc.usage == TextureUsage::None) {
        Log::fatal("VulkanDevice", "Invalid or unsupported Texture description");
    }
    if ((isColorFormat(desc.format) && hasFlag(desc.usage, TextureUsage::DepthStencilAttachment)) ||
        (isDepthFormat(desc.format) && hasFlag(desc.usage, TextureUsage::ColorAttachment))) {
        Log::fatal("VulkanDevice", "Texture format and attachment usage do not match");
    }
    const RID handle =
        textures_.emplace(*this, allocator_, desc, toVulkan(desc.format), toVulkan(desc.usage));
    VulkanTexture* texture = textures_.find(handle);
    const TextureViewDesc defaultDesc{.type = desc.dimension,
                                      .format = desc.format,
                                      .baseMip = 0,
                                      .mipCount = desc.mipCount,
                                      .baseLayer = 0,
                                      .layerCount = desc.arrayLayers};
    texture->setDefaultView(createTextureView(handle, defaultDesc));
    return handle;
}

void VulkanDevice::destroyTexture(RID handle) {
    VulkanTexture* texture = textures_.find(handle);
    if (!texture)
        return;
    for (RID view : texture->viewHandles())
        (void)textureViews_.release(view);
    (void)textures_.release(handle);
}

void VulkanDevice::uploadTexture(RID destination,
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
    const RID staging = createBuffer(stagingDesc);
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

    std::unique_ptr<ICommandBuffer> command = createCommandBuffer();
    command->begin();
    const TextureBarrier toCopyDestination{destination,
                                           TextureAspect::Color,
                                           ResourceState::Undefined,
                                           ResourceState::CopyDestination,
                                           0,
                                           kRemainingMipLevels,
                                           0,
                                           1};
    command->resourceBarriers(std::span{&toCopyDestination, 1});
    for (const BufferImageCopy& copy : copies) {
        command->copyBufferToImage(copy);
    }
    const TextureBarrier toShaderRead{destination,
                                      TextureAspect::Color,
                                      ResourceState::CopyDestination,
                                      ResourceState::ShaderRead,
                                      0,
                                      kRemainingMipLevels,
                                      0,
                                      1};
    command->resourceBarriers(std::span{&toShaderRead, 1});
    command->end();
    submitCommand(*command, SubmitSync{});
    waitIdle();

    destroyBuffer(staging);
    resource->markUploaded();
}

RID VulkanDevice::createTextureView(RID textureHandle,
                                                  const TextureViewDesc& desc) {
    VulkanTexture* texture = textures_.find(textureHandle);
    if (!texture)
        Log::fatal("VulkanDevice", "Invalid RHI Texture handle");
    return texture->createView(desc);
}

RID VulkanDevice::acquireTextureView(VulkanTexture& texture,
                                                   const TextureViewDesc& desc) {
    if (desc.type != TextureType::Texture2D || desc.mipCount == 0 || desc.layerCount != 1 ||
        desc.baseLayer != 0) {
        Log::fatal("VulkanDevice", "Invalid or unsupported TextureView description");
    }
    TextureViewDesc normalized = desc;
    if (normalized.format == PixelFormat::Undefined)
        normalized.format = texture.format();
    if (normalized.format != texture.format() || normalized.baseMip >= texture.mipCount() ||
        normalized.mipCount > texture.mipCount() - normalized.baseMip) {
        Log::fatal("VulkanDevice", "TextureView does not match its Texture");
    }
    if (const RID existing = texture.findView(normalized))
        return existing;
    const RID view =
        textureViews_.insert(VulkanTextureView{device_, texture, normalized});
    texture.cacheView(normalized, view);
    return view;
}

RID VulkanDevice::defaultTextureView(RID texture) const {
    const VulkanTexture* resource = textures_.find(texture);
    if (!resource || !resource->defaultView())
        Log::fatal("VulkanDevice", "Texture has no default view");
    return resource->defaultView();
}

void VulkanDevice::destroyTextureView(RID handle) {
    VulkanTextureView* view = textureViews_.find(handle);
    if (!view)
        return;
    if (IRHITexture* texture = view->texture())
        static_cast<VulkanTexture*>(texture)->removeView(handle);
    (void)textureViews_.release(handle);
}

RID VulkanDevice::createSampler(const SamplerDesc& desc) {
    SamplerDesc clamped = desc;
    clamped.maxAnisotropy = std::min(clamped.maxAnisotropy, maxSamplerAnisotropy_);
    if (clamped.maxAnisotropy < 1.0F)
        clamped.maxAnisotropy = 1.0F;
    // Identical descriptors share one VkSampler; only create on a cache miss.
    if (const RID existing = samplers_.findHandle(clamped))
        return existing;
    return samplers_.insert(VulkanSampler{device_, clamped, maxSamplerAnisotropy_});
}

void VulkanDevice::destroySampler(RID handle) {
    (void)samplers_.destroy(handle);
}

RID VulkanDevice::createShader(const ShaderDesc& desc) {
    if (desc.bytecode.empty()) {
        Log::fatal("VulkanDevice", "Cannot create an empty shader");
    }
    return shaders_.insert(ShaderResource{
        std::make_unique<VulkanShaderModule>(device_, desc.stage, desc.bytecode, desc.debugName)});
}

void VulkanDevice::destroyShader(RID handle) {
    (void)shaders_.release(handle);
}

RID VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (!desc.vertexShader || !desc.fragmentShader ||
        (desc.colorFormats.empty() && desc.depthFormat == PixelFormat::Undefined) ||
        std::ranges::any_of(desc.colorFormats,
                            [](PixelFormat format) { return !isColorFormat(format); }) ||
        (desc.depthFormat != PixelFormat::Undefined && !isDepthFormat(desc.depthFormat))) {
        Log::fatal("VulkanDevice", "Invalid graphics pipeline description");
    }
    const PipelineLayoutKey key(desc.bindGroupLayouts.begin(), desc.bindGroupLayouts.end());
    return pipelines_.insert(PipelineResource{
        std::make_unique<VulkanGraphicsPipeline>(device_,
                                                 desc,
                                                 resolveShader(desc.vertexShader),
                                                 resolveShader(desc.fragmentShader),
                                                 acquirePipelineLayout(key),
                                                 pipelineCache_)});
}

void VulkanDevice::destroyGraphicsPipeline(RID handle) {
    (void)pipelines_.release(handle);
}

RID VulkanDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
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

void VulkanDevice::destroyBindGroupLayout(RID handle) {
    // Pipelines must be destroyed before the layouts they reference; drop the
    // cached pipeline layouts that still point at this descriptor set layout.
    destroyPipelineLayoutsReferencing(handle);
    (void)bindGroupLayouts_.release(handle);
}

RID VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
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

void VulkanDevice::destroyBindGroup(RID handle) {
    BindGroupResource* resource = bindGroups_.find(handle);
    if (!resource)
        return;
    descriptorAllocator_->free(resource->resource);
    (void)bindGroups_.release(handle);
}

std::unique_ptr<ICommandBuffer> VulkanDevice::createCommandBuffer() {
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    check(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer),
          "vkAllocateCommandBuffers");
    return std::make_unique<VulkanCommandBuffer>(commandBuffer, *this, /*owned=*/true);
}

void VulkanDevice::submitCommand(ICommandBuffer& command, const SubmitSync& sync) {
    if (command.state() != CommandState::Executable) {
        Log::fatal("VulkanDevice",
                   "submitCommand requires a command buffer that finished recording (call end() "
                   "first)");
    }
    const auto* native = dynamic_cast<const IVulkanCommandBuffer*>(&command);
    if (!native) {
        Log::fatal("VulkanDevice", "submitCommand requires a Vulkan command buffer");
    }
    VkCommandBuffer nativeCommandBuffer = native->nativeCommandBuffer();
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

RID VulkanDevice::acquireStagingBuffer(std::uint64_t size) {
    if (size == 0) {
        Log::fatal("VulkanDevice", "Staging buffer size must be positive");
    }
    const BufferDesc desc{
        size, BufferUsage::TransferSource, MemoryUsage::Upload, "command buffer staging"};
    const RID handle = createBuffer(desc);
    pendingStagingBuffers_.push_back(StagingBuffer{handle, VK_NULL_HANDLE});
    return handle;
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
            destroyBuffer(entry->handle);
            entry = pending.erase(entry);
            continue;
        }
        ++entry;
    }
}

VulkanBuffer& VulkanDevice::requireBuffer(RID handle) {
    return const_cast<VulkanBuffer&>(std::as_const(*this).requireBuffer(handle));
}

VulkanDevice::BufferResource& VulkanDevice::requireBufferResource(RID handle) {
    return const_cast<BufferResource&>(std::as_const(*this).requireBufferResource(handle));
}

const VulkanDevice::BufferResource& VulkanDevice::requireBufferResource(RID handle) const {
    const BufferResource* resource = buffers_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI buffer handle");
    }
    return *resource;
}

const VulkanBuffer& VulkanDevice::requireBuffer(RID handle) const {
    return *requireBufferResource(handle).resource;
}

VkBuffer VulkanDevice::resolveBuffer(RID handle) const {
    return requireBuffer(handle).handle();
}

IRHITexture* VulkanDevice::resolveTextureResource(RID handle) {
    return textures_.find(handle);
}

const IRHITexture* VulkanDevice::resolveTextureResource(RID handle) const {
    return textures_.find(handle);
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
    const ShaderResource* resource = shaders_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI shader handle");
    }
    return resource->resource->handle();
}

VkDescriptorSetLayout VulkanDevice::resolveBindGroupLayout(RID handle) const {
    const BindGroupLayoutResource* resource = bindGroupLayouts_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI bind group layout handle");
    }
    return resource->resource->handle();
}

ResolvedPipeline VulkanDevice::resolvePipeline(RID handle) const {
    const PipelineResource* resource = pipelines_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI graphics pipeline handle");
    }
    return {resource->resource->handle(), resource->resource->layout()};
}

VkDescriptorSet VulkanDevice::resolveBindGroup(RID handle) const {
    const BindGroupResource* resource = bindGroups_.find(handle);
    if (!resource) {
        Log::fatal("VulkanDevice", "Invalid or stale RHI bind group handle");
    }
    return resource->resource;
}

RID VulkanDevice::registerExternalTexture(VkImage image,
                                                    const TextureDesc& desc,
                                                    VkFormat nativeFormat) {
    return textures_.emplace(*this, image, desc, nativeFormat);
}

void VulkanDevice::unregisterExternalTexture(RID handle) {
    destroyTexture(handle);
}

RID VulkanDevice::registerExternalTextureView(RID textureHandle,
                                                            VkImageView view,
                                                            const TextureViewDesc& desc) {
    VulkanTexture* texture = textures_.find(textureHandle);
    if (!texture)
        Log::fatal("VulkanDevice", "Cannot register a view for an invalid external texture");
    TextureViewDesc normalized = desc;
    if (normalized.format == PixelFormat::Undefined)
        normalized.format = texture->format();
    const RID handle =
        textureViews_.insert(VulkanTextureView{device_, *texture, view, normalized});
    texture->cacheView(normalized, handle);
    texture->setDefaultView(handle);
    return handle;
}

void VulkanDevice::unregisterExternalTextureView(RID handle) {
    destroyTextureView(handle);
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
    pipelines_.clear();
    for (auto& [key, layout] : pipelineLayouts_)
        vkDestroyPipelineLayout(device_, layout, nullptr);
    pipelineLayouts_.clear();
    bindGroups_.forEach([this](const BindGroupResource& resource) {
        descriptorAllocator_->free(resource.resource);
    });
    bindGroups_.clear();
    descriptorAllocator_.reset();
    bindGroupLayouts_.clear();
    shaders_.clear();
    samplers_.clear();
    textureViews_.clear();
    textures_.clear();
    buffers_.clear();
}

} // namespace engine::rhi::vulkan
