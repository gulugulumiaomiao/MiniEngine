#include "render/backend/vulkan/VulkanBackend.h"

#include "core/logging/Log.h"
#include "core/filesystem/FileSystem.h"
#include "runtime/window/Window.h"
#include "render/render_graph/RenderGraph.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompiler.h"
#include "rhi/vulkan/Buffer.h"
#include "rhi/vulkan/DescriptorAllocator.h"
#include "rhi/vulkan/GpuAllocator.h"
#include "render/backend/vulkan/GraphicsPipeline.h"
#include "render/backend/vulkan/PipelineCache.h"
#include "render/backend/vulkan/RhiShaderCache.h"
#include "render/backend/vulkan/MaterialGpuCache.h"
#include "render/backend/vulkan/MeshGpuCache.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#if defined(MINI_DEBUG)
#include <iostream>
#endif
#include <iterator>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace engine {
namespace {

#if defined(MINI_DEBUG)
constexpr std::array kValidationLayers{"VK_LAYER_KHRONOS_validation"};
#endif
constexpr std::array kDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        Log::fatal("VulkanBackend", std::string(operation) + " failed (VkResult " +
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
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}
#endif

} // namespace

VulkanBackend::VulkanBackend(Window& window, bool vsync)
    : vsync_(vsync), window_(window) {
    createInstance();
#if defined(MINI_DEBUG)
    createDebugMessenger();
#endif
    createSurface();
    selectPhysicalDevice();
    createDevice();
    createGpuResources();
    createSceneDescriptors();
    createSwapchain();
    if (!FILE_SYSTEM.mountDirectory("shader", MINI_GENERATED_SHADER_DIR, false)) {
        Log::fatal("VulkanBackend", "Cannot mount generated Shader directory: %s",
                   MINI_GENERATED_SHADER_DIR);
    }
    compiledShaderCache_ = std::make_unique<CompiledShaderCache>();
    shaderProgramCache_ =
        std::make_unique<ShaderProgramCache>(*compiledShaderCache_);
    rhiShaderCache_ =
        std::make_unique<RhiShaderCache>(device_, *compiledShaderCache_);
    materialGpuCache_ = std::make_unique<MaterialGpuCache>(
        device_, allocator_->handle(), materialDescriptorLayout_->handle(),
        kFramesInFlight);
    meshGpuCache_ = std::make_unique<MeshGpuCache>(*this);
    pipelineCache_ = std::make_unique<PipelineCache>(
        device_, sceneDescriptorLayout_->handle(),
        materialDescriptorLayout_->handle(), *compiledShaderCache_,
        *shaderProgramCache_, *rhiShaderCache_);
    createCommandResources();
    createSyncObjects();
}

VulkanBackend::~VulkanBackend() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        for (const auto& frame : frames_) {
            vkDestroyFence(device_, frame.inFlight, nullptr);
            vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        }
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        pipelineCache_->clear();
        pipelineCache_.reset();
        materialGpuCache_->clear();
        materialGpuCache_.reset();
        meshGpuCache_->clear();
        meshGpuCache_.reset();
        rhiShaderCache_->clear();
        rhiShaderCache_.reset();
        shaderProgramCache_->clear();
        shaderProgramCache_.reset();
        compiledShaderCache_->clear();
        compiledShaderCache_.reset();
        destroySwapchain();
        buffers_.clear();
        for (auto& frame : frames_) {
            frame.sceneDescriptor = VK_NULL_HANDLE;
            frame.descriptorAllocator.reset();
            frame.sceneBuffer.reset();
            frame.objectBuffer.reset();
        }
        sceneDescriptorLayout_.reset();
        materialDescriptorLayout_.reset();
        allocator_.reset();
        vkDestroyDevice(device_, nullptr);
    }
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
#if defined(MINI_DEBUG)
    if (debugMessenger_ != VK_NULL_HANDLE) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) {
            destroy(instance_, debugMessenger_, nullptr);
        }
    }
#endif
    vkDestroyInstance(instance_, nullptr);
}

void VulkanBackend::createInstance() {
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
    {
        std::uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        const bool found = std::ranges::any_of(availableLayers, [](const VkLayerProperties& layer) {
            return std::strcmp(layer.layerName, kValidationLayers[0]) == 0;
        });
        if (!found) {
            Log::fatal("VulkanBackend",
                       "Vulkan validation layer is unavailable; install the Vulkan SDK");
        }
        createInfo.enabledLayerCount = static_cast<std::uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
        createInfo.pNext = &debugInfo;
    }
#endif
    check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

#if defined(MINI_DEBUG)
void VulkanBackend::createDebugMessenger() {
    const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!create) {
        Log::fatal("VulkanBackend", "VK_EXT_debug_utils is unavailable");
    }
    auto info = debugCreateInfo();
    check(create(instance_, &info, nullptr, &debugMessenger_), "vkCreateDebugUtilsMessengerEXT");
}
#endif

void VulkanBackend::createSurface() {
    VkWin32SurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    createInfo.hinstance = window_.nativeInstance();
    createInfo.hwnd = window_.nativeHandle();
    check(vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_),
          "vkCreateWin32SurfaceKHR");
}

VulkanBackend::QueueFamilies VulkanBackend::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilies result;
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

    for (std::uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            result.graphics = i;
        }
        VkBool32 supportsPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &supportsPresent);
        if (supportsPresent == VK_TRUE) {
            result.present = i;
        }
        if (result.complete()) {
            break;
        }
    }
    return result;
}

VulkanBackend::SwapchainSupport VulkanBackend::querySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupport result;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &result.capabilities);

    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count, nullptr);
    result.formats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count, result.formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count, nullptr);
    result.presentModes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count, result.presentModes.data());
    return result;
}

bool VulkanBackend::isDeviceSuitable(VkPhysicalDevice device) const {
    if (!findQueueFamilies(device).complete()) {
        return false;
    }

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

    const auto support = querySwapchainSupport(device);
    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    return !support.formats.empty() && !support.presentModes.empty() && features13.dynamicRendering;
}

void VulkanBackend::selectPhysicalDevice() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            break;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanBackend",
                   "No Vulkan 1.3 GPU with swapchain and dynamic rendering support found");
    }
}

void VulkanBackend::createDevice() {
    const auto families = findQueueFamilies(physicalDevice_);
    const std::set uniqueFamilies{*families.graphics, *families.present};
    constexpr float priority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (std::uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queueInfos.push_back(info);
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    check(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, *families.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, *families.present, 0, &presentQueue_);
}

void VulkanBackend::createGpuResources() {
    allocator_ = std::make_unique<GpuAllocator>(instance_, physicalDevice_, device_);
}

void VulkanBackend::createSceneDescriptors() {
    VkDescriptorSetLayoutBinding sceneBinding{};
    sceneBinding.binding = 0;
    sceneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sceneBinding.descriptorCount = 1;
    sceneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                              VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding objectBinding{};
    objectBinding.binding = 1;
    objectBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    objectBinding.descriptorCount = 1;
    objectBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    const std::array bindings{sceneBinding, objectBinding};
    sceneDescriptorLayout_ = std::make_unique<DescriptorSetLayout>(device_, bindings);

    std::array<VkDescriptorSetLayoutBinding, 17> materialBindings{};
    materialBindings[0].binding = 0;
    materialBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    materialBindings[0].descriptorCount = 1;
    materialBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                     VK_SHADER_STAGE_FRAGMENT_BIT;
    for (std::uint32_t binding = 1; binding < materialBindings.size(); ++binding) {
        materialBindings[binding].binding = binding;
        materialBindings[binding].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[binding].descriptorCount = 1;
        materialBindings[binding].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    materialDescriptorLayout_ =
        std::make_unique<DescriptorSetLayout>(device_, materialBindings);

    const VkDeviceSize bufferSize = sizeof(ObjectDrawData) * kMaxRenderObjects;
    for (auto& frame : frames_) {
        frame.sceneBuffer = std::make_unique<Buffer>(
            allocator_->handle(), sizeof(SceneDrawData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        frame.objectBuffer = std::make_unique<Buffer>(
            allocator_->handle(), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        frame.descriptorAllocator = std::make_unique<DescriptorAllocator>(device_);
    }
}

void VulkanBackend::prepareSceneDescriptor(FrameContext& frame) {
    frame.descriptorAllocator->reset();
    ++frame.descriptorGeneration;
    frame.sceneDescriptor =
        frame.descriptorAllocator->allocate(sceneDescriptorLayout_->handle());

    const VkDeviceSize bufferSize = sizeof(ObjectDrawData) * kMaxRenderObjects;
    VkDescriptorBufferInfo sceneInfo{frame.sceneBuffer->handle(), 0,
                                     sizeof(SceneDrawData)};
    VkDescriptorBufferInfo objectInfo{frame.objectBuffer->handle(), 0,
                                      bufferSize};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = frame.sceneDescriptor;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &sceneInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = frame.sceneDescriptor;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &objectInfo;
    vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

rhi::BufferHandle VulkanBackend::createGpuBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                  VmaMemoryUsage memoryUsage,
                                                  VmaAllocationCreateFlags flags) {
    auto slot = std::ranges::find_if(buffers_, [](const BufferSlot& candidate) {
        return !candidate.alive;
    });
    if (slot == buffers_.end()) {
        buffers_.emplace_back();
        slot = std::prev(buffers_.end());
    }
    slot->resource = std::make_unique<Buffer>(allocator_->handle(), size, usage, memoryUsage,
                                              flags);
    slot->alive = true;
    return {static_cast<std::uint32_t>(std::distance(buffers_.begin(), slot)),
            slot->generation};
}

void VulkanBackend::destroyGpuBuffer(rhi::BufferHandle handle) {
    if (handle.index >= buffers_.size()) {
        return;
    }
    BufferSlot& slot = buffers_[handle.index];
    if (!slot.alive || slot.generation != handle.generation) {
        return;
    }
    slot.resource.reset();
    slot.alive = false;
    ++slot.generation;
}

Buffer& VulkanBackend::requireBuffer(rhi::BufferHandle handle) {
    return const_cast<Buffer&>(std::as_const(*this).requireBuffer(handle));
}

const Buffer& VulkanBackend::requireBuffer(rhi::BufferHandle handle) const {
    if (handle.index >= buffers_.size()) {
        Log::fatal("VulkanBackend", "Invalid RHI buffer handle");
    }
    const BufferSlot& slot = buffers_[handle.index];
    if (!slot.alive || slot.generation != handle.generation || !slot.resource) {
        Log::fatal("VulkanBackend", "Stale RHI buffer handle");
    }
    return *slot.resource;
}

void VulkanBackend::uploadBuffer(rhi::BufferHandle source, rhi::BufferHandle destination,
                                 VkDeviceSize size) {
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
    rhi::vulkan::VulkanTransferCommandEncoder encoder(commandBuffer, *this);
    encoder.copyBuffer({source, destination, 0, 0, size});
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(upload)");

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE),
          "vkQueueSubmit(upload)");
    check(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle(upload)");
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void VulkanBackend::uploadObjectData(FrameContext& frame,
                                     std::span<const ObjectDrawData> objects) {
    if (objects.size() > kMaxRenderObjects) {
        Log::fatal("VulkanBackend", "DrawList exceeds kMaxRenderObjects");
    }
    if (!objects.empty()) {
        frame.objectBuffer->upload(std::as_bytes(objects));
    }
}

void VulkanBackend::uploadSceneData(FrameContext& frame,
                                    const SceneDrawData& scene) {
    frame.sceneBuffer->upload(std::as_bytes(std::span{&scene, 1}));
}

MeshDrawInfo VulkanBackend::prepareMesh(MeshHandle handle, Mesh& mesh) {
    return meshGpuCache_->prepare(handle, mesh);
}

void VulkanBackend::releaseMesh(MeshHandle handle) {
    meshGpuCache_->invalidate(handle);
}

rhi::GraphicsPipelineHandle VulkanBackend::pipelineForPass(
    const Shader& shader, const ShaderPass& pass,
    const ShaderVariantKey& variant, const VertexLayout& vertexLayout) {
    refreshShaderCaches();
    return pipelineCache_->getOrCreate(shader, pass, variant,
                                       vertexLayout, swapchainFormat_);
}

void VulkanBackend::refreshShaderCaches() {
    if (lastShaderPollSerial_ == frameSerial_) return;
    lastShaderPollSerial_ = frameSerial_;
    const std::vector<CompiledShaderId> changed =
        compiledShaderCache_->invalidateChanged();
    if (changed.empty()) return;
    const std::uint64_t retireSerial = frameSerial_ + kFramesInFlight;
    pipelineCache_->invalidate(changed, retireSerial);
    rhiShaderCache_->invalidate(changed, retireSerial);
    shaderProgramCache_->invalidate(changed);
    Log::info("VulkanBackend", "Reloaded %zu changed shader stages",
              changed.size());
}

void VulkanBackend::createSwapchain() {
    const auto support = querySwapchainSupport(physicalDevice_);
    const auto formatIt = std::ranges::find_if(support.formats, [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
               format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    const VkSurfaceFormatKHR surfaceFormat =
        formatIt != support.formats.end() ? *formatIt : support.formats.front();

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_ &&
        std::ranges::find(support.presentModes, VK_PRESENT_MODE_MAILBOX_KHR) !=
            support.presentModes.end()) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }

    if (support.capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        swapchainExtent_ = support.capabilities.currentExtent;
    } else {
        const auto [width, height] = window_.framebufferSize();
        swapchainExtent_.width = std::clamp(width,
                                            support.capabilities.minImageExtent.width,
                                            support.capabilities.maxImageExtent.width);
        swapchainExtent_.height = std::clamp(height,
                                             support.capabilities.minImageExtent.height,
                                             support.capabilities.maxImageExtent.height);
    }

    std::uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const auto families = findQueueFamilies(physicalDevice_);
    const std::array queueIndices{*families.graphics, *families.present};
    if (families.graphics != families.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueIndices.size());
        createInfo.pQueueFamilyIndices = queueIndices.data();
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());
    swapchainFormat_ = surfaceFormat.format;
    swapchainImageInitialized_.assign(imageCount, false);

    swapchainImageViews_.resize(imageCount);
    renderFinishedSemaphores_.resize(imageCount);
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device_, &viewInfo, nullptr, &swapchainImageViews_[i]),
              "vkCreateImageView");
        check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]),
              "vkCreateSemaphore(renderFinished)");
    }
}

void VulkanBackend::destroySwapchain() {
    for (VkSemaphore semaphore : renderFinishedSemaphores_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    renderFinishedSemaphores_.clear();
    for (VkImageView view : swapchainImageViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    swapchainImageInitialized_.clear();
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    ++swapchainGeneration_;
}

void VulkanBackend::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = *findQueueFamilies(physicalDevice_).graphics;
    check(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_), "vkCreateCommandPool");

    std::array<VkCommandBuffer, kFramesInFlight> buffers{};
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    check(vkAllocateCommandBuffers(device_, &allocInfo, buffers.data()), "vkAllocateCommandBuffers");
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        frames_[i].commandBuffer = buffers[i];
    }
}

void VulkanBackend::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& frame : frames_) {
        check(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAvailable),
              "vkCreateSemaphore(imageAvailable)");
        check(vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlight), "vkCreateFence");
    }
}

void VulkanBackend::recordDrawCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex,
                                       const DrawList& drawList) {
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    const rhi::TextureHandle backBuffer{imageIndex, swapchainGeneration_};
    const rhi::TextureViewHandle backBufferView{imageIndex, swapchainGeneration_};
    RenderGraph graph;
    graph.importTexture({
        .texture = backBuffer,
        .initialState = swapchainImageInitialized_[imageIndex]
                            ? rhi::ResourceState::Present
                            : rhi::ResourceState::Undefined,
        .finalState = rhi::ResourceState::Present,
        .aspect = rhi::TextureAspect::Color,
    });

    rhi::RenderingInfo rendering;
    rendering.renderArea = {0, 0, swapchainExtent_.width, swapchainExtent_.height};
    rendering.colorAttachments.push_back(
        {backBufferView, rhi::LoadOp::Clear, rhi::StoreOp::Store,
         drawList.clearColor});
    graph.addGraphicsPass(
        "Forward", std::move(rendering),
        {{backBuffer, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [this, &drawList](rhi::IGraphicsCommandEncoder& encoder) {
            encoder.setViewport({0.0F, 0.0F, static_cast<float>(swapchainExtent_.width),
                                 static_cast<float>(swapchainExtent_.height), 0.0F, 1.0F});
            encoder.setScissor({0, 0, swapchainExtent_.width, swapchainExtent_.height});
            rhi::GraphicsPipelineHandle boundPipeline;
            MaterialHandle boundMaterial;
            for (const DrawItem& item : drawList.items) {
                if (item.pipeline != boundPipeline) {
                    encoder.bindPipeline(item.pipeline);
                    encoder.bindGroup(0, {0,
                        frames_[currentFrame_].descriptorGeneration});
                    boundPipeline = item.pipeline;
                }
                if (item.material.index != boundMaterial.index ||
                    item.material.generation != boundMaterial.generation) {
                    const Material* material = MATERIAL_MANAGER.find(item.material);
                    if (!material) {
                        Log::fatal("VulkanBackend",
                                   "DrawItem contains a stale MaterialHandle");
                    }
                    encoder.bindGroup(1,
                        materialGpuCache_->prepare(item.material, *material));
                    boundMaterial = item.material;
                }
                for (const DrawItem::VertexBuffer& vertex : item.vertexBuffers) {
                    encoder.bindVertexBuffer(vertex.binding, vertex.buffer);
                }
                encoder.bindIndexBuffer(item.indexBuffer, 0, item.indexFormat);
                encoder.drawIndexed(item.arguments);
            }
        });

    rhi::vulkan::VulkanGraphicsCommandEncoder encoder(commandBuffer, *this);
    graph.execute(encoder);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    swapchainImageInitialized_[imageIndex] = true;
}

void VulkanBackend::renderFrame(const DrawList& drawList) {
    auto& frame = frames_[currentFrame_];
    check(vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    pipelineCache_->collect(frameSerial_);
    rhiShaderCache_->collect(frameSerial_);

    std::uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                                    frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        check(acquire, "vkAcquireNextImageKHR");
    }

    check(vkResetFences(device_, 1, &frame.inFlight), "vkResetFences");
    prepareSceneDescriptor(frame);
    materialGpuCache_->beginFrame(currentFrame_, *frame.descriptorAllocator,
                                  frame.descriptorGeneration);
    uploadSceneData(frame, drawList.scene);
    uploadObjectData(frame, drawList.objects);
    check(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer");
    recordDrawCommands(frame.commandBuffer, imageIndex, drawList);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    const VkSemaphore renderFinished = renderFinishedSemaphores_[imageIndex];
    submitInfo.pSignalSemaphores = &renderFinished;
    check(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.inFlight), "vkQueueSubmit");

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult present = vkQueuePresentKHR(presentQueue_, &presentInfo);
    const bool resized = window_.consumeResize();
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || resized) {
        recreateSwapchain();
    } else if (present != VK_SUCCESS) {
        check(present, "vkQueuePresentKHR");
    }
    currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
    ++frameSerial_;
}

void VulkanBackend::recreateSwapchain() {
    window_.waitForUsableFramebuffer();
    if (window_.shouldClose()) {
        return;
    }
    check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    pipelineCache_->clear();
    destroySwapchain();
    createSwapchain();
}

VkBuffer VulkanBackend::resolveBuffer(rhi::BufferHandle handle) const {
    return requireBuffer(handle).handle();
}

VkImage VulkanBackend::resolveTexture(rhi::TextureHandle handle) const {
    if (handle.generation != swapchainGeneration_ || handle.index >= swapchainImages_.size()) {
        Log::fatal("VulkanBackend", "Invalid or stale RHI texture handle");
    }
    return swapchainImages_[handle.index];
}

VkImageView VulkanBackend::resolveTextureView(rhi::TextureViewHandle handle) const {
    if (handle.generation != swapchainGeneration_ ||
        handle.index >= swapchainImageViews_.size()) {
        Log::fatal("VulkanBackend", "Invalid or stale RHI texture view handle");
    }
    return swapchainImageViews_[handle.index];
}

rhi::vulkan::ResolvedPipeline VulkanBackend::resolvePipeline(
    rhi::GraphicsPipelineHandle handle) const {
    const GraphicsPipeline& pipeline = pipelineCache_->resolve(handle);
    return {pipeline.handle(), pipeline.layout()};
}

VkDescriptorSet VulkanBackend::resolveBindGroup(rhi::BindGroupHandle handle) const {
    const FrameContext& frame = frames_[currentFrame_];
    if (handle.index != 0) {
        return materialGpuCache_->resolve(handle, currentFrame_);
    }
    if (frame.descriptorGeneration != handle.generation ||
        frame.sceneDescriptor == VK_NULL_HANDLE) {
        Log::fatal("VulkanBackend", "Invalid or stale RHI bind group handle");
    }
    return frame.sceneDescriptor;
}

void VulkanBackend::waitIdle() {
    check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
}

} // namespace engine
