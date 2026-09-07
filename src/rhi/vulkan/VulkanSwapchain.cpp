#include "rhi/vulkan/VulkanSwapchain.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string>

namespace engine::rhi::vulkan {
namespace {

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        Log::fatal("VulkanSwapchain",
                   std::string(operation) + " failed (VkResult " +
                       std::to_string(static_cast<int>(result)) + ")");
    }
}

TextureFormat toRhi(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::Rgba8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return TextureFormat::Rgba8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::Bgra8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return TextureFormat::Bgra8Srgb;
    default: Log::fatal("VulkanSwapchain", "Unsupported swapchain format");
    }
}

} // namespace

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, const SwapchainDesc& desc)
    : device_(device), desc_(desc) {
    createFrameResources();
    create();
}

VulkanSwapchain::~VulkanSwapchain() {
    device_.waitIdle();
    encoder_.reset();
    destroy();
    for (const Frame& frame : frames_) {
        vkDestroyFence(device(), frame.inFlight, nullptr);
        vkDestroySemaphore(device(), frame.imageAvailable, nullptr);
    }
}

VulkanSwapchain::Support VulkanSwapchain::querySupport() const {
    Support result;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device_.physicalDevice(), device_.surface(), &result.capabilities);
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device_.physicalDevice(), device_.surface(), &count, nullptr);
    result.formats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device_.physicalDevice(), device_.surface(), &count, result.formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device_.physicalDevice(), device_.surface(), &count, nullptr);
    result.presentModes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device_.physicalDevice(), device_.surface(), &count, result.presentModes.data());
    return result;
}

void VulkanSwapchain::create() {
    const Support support = querySupport();
    if (support.formats.empty() || support.presentModes.empty()) {
        Log::fatal("VulkanSwapchain", "Surface has no supported format or present mode");
    }
    const auto formatIt = std::ranges::find_if(support.formats, [](const auto& candidate) {
        return candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
               candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    const VkSurfaceFormatKHR surfaceFormat =
        formatIt == support.formats.end() ? support.formats.front() : *formatIt;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!desc_.vsync && std::ranges::find(support.presentModes, VK_PRESENT_MODE_MAILBOX_KHR) !=
                            support.presentModes.end()) {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (support.capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extent_ = support.capabilities.currentExtent;
    } else {
        extent_ = {
            std::clamp(desc_.width,
                       support.capabilities.minImageExtent.width,
                       support.capabilities.maxImageExtent.width),
            std::clamp(desc_.height,
                       support.capabilities.minImageExtent.height,
                       support.capabilities.maxImageExtent.height),
        };
    }
    std::uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);
    }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = device_.surface();
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array queueFamilies{device_.graphicsQueueFamily(), device_.presentQueueFamily()};
    if (queueFamilies[0] != queueFamilies[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilies.size());
        info.pQueueFamilyIndices = queueFamilies.data();
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = support.capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device(), &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    vkGetSwapchainImagesKHR(device(), swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device(), swapchain_, &imageCount, images_.data());
    format_ = surfaceFormat.format;
    imageInitialized_.assign(imageCount, false);
    imageViews_.resize(imageCount);
    renderFinished_.resize(imageCount);
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device(), &viewInfo, nullptr, &imageViews_[i]),
              "vkCreateImageView");
        check(vkCreateSemaphore(device(), &semaphoreInfo, nullptr, &renderFinished_[i]),
              "vkCreateSemaphore(renderFinished)");
    }
}

void VulkanSwapchain::destroy() {
    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(device(), semaphore, nullptr);
    }
    renderFinished_.clear();
    for (VkImageView view : imageViews_)
        vkDestroyImageView(device(), view, nullptr);
    imageViews_.clear();
    images_.clear();
    imageInitialized_.clear();
    if (swapchain_ != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    ++generation_;
}

void VulkanSwapchain::createFrameResources() {
    std::array<VkCommandBuffer, kFramesInFlight> buffers{};
    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = device_.commandPool();
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = kFramesInFlight;
    check(vkAllocateCommandBuffers(device(), &allocateInfo, buffers.data()),
          "vkAllocateCommandBuffers");
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        frames_[i].commandBuffer = buffers[i];
        check(vkCreateSemaphore(device(), &semaphoreInfo, nullptr, &frames_[i].imageAvailable),
              "vkCreateSemaphore(imageAvailable)");
        check(vkCreateFence(device(), &fenceInfo, nullptr, &frames_[i].inFlight), "vkCreateFence");
    }
}

FrameStatus VulkanSwapchain::beginFrame() {
    if (frameOpen_)
        Log::fatal("VulkanSwapchain", "A frame is already open");
    Frame& frame = frames_[currentFrame_];
    check(vkWaitForFences(device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    const VkResult acquire = vkAcquireNextImageKHR(
        device(), swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
        return FrameStatus::OutOfDate;
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        check(acquire, "vkAcquireNextImageKHR");
    }
    check(vkResetFences(device(), 1, &frame.inFlight), "vkResetFences");
    check(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    encoder_ = std::make_unique<VulkanGraphicsCommandEncoder>(
        frame.commandBuffer, static_cast<const IVulkanResourceResolver&>(*this));
    frameOpen_ = true;
    return FrameStatus::Ready;
}

FrameStatus VulkanSwapchain::endFrame() {
    if (!frameOpen_)
        Log::fatal("VulkanSwapchain", "No frame is open");
    Frame& frame = frames_[currentFrame_];
    encoder_.reset();
    check(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSemaphore finished = renderFinished_[imageIndex_];
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &finished;
    check(vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, frame.inFlight), "vkQueueSubmit");
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &finished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex_;
    const VkResult present = vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
    imageInitialized_[imageIndex_] = true;
    frameOpen_ = false;
    currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
        return FrameStatus::OutOfDate;
    }
    check(present, "vkQueuePresentKHR");
    return FrameStatus::Ready;
}

void VulkanSwapchain::resize(std::uint32_t width, std::uint32_t height) {
    if (frameOpen_ || width == 0 || height == 0)
        return;
    device_.waitIdle();
    desc_.width = width;
    desc_.height = height;
    destroy();
    create();
}

IGraphicsCommandEncoder& VulkanSwapchain::encoder() {
    if (!encoder_)
        Log::fatal("VulkanSwapchain", "No active command encoder");
    return *encoder_;
}

TextureHandle VulkanSwapchain::currentTexture() const {
    return {imageIndex_, generation_};
}
TextureViewHandle VulkanSwapchain::currentTextureView() const {
    return {imageIndex_, generation_};
}
ResourceState VulkanSwapchain::currentTextureState() const {
    return imageInitialized_[imageIndex_] ? ResourceState::Present : ResourceState::Undefined;
}
TextureFormat VulkanSwapchain::format() const {
    return toRhi(format_);
}
VkDevice VulkanSwapchain::device() const {
    return device_.device();
}
VkBuffer VulkanSwapchain::resolveBuffer(BufferHandle handle) const {
    return device_.resolveBuffer(handle);
}
VkImage VulkanSwapchain::resolveTexture(TextureHandle handle) const {
    if (handle.generation != generation_ || handle.index >= images_.size()) {
        Log::fatal("VulkanSwapchain", "Invalid or stale swapchain texture handle");
    }
    return images_[handle.index];
}
VkImageView VulkanSwapchain::resolveTextureView(TextureViewHandle handle) const {
    if (handle.generation != generation_ || handle.index >= imageViews_.size()) {
        Log::fatal("VulkanSwapchain", "Invalid or stale swapchain texture view handle");
    }
    return imageViews_[handle.index];
}
ResolvedPipeline VulkanSwapchain::resolvePipeline(GraphicsPipelineHandle handle) const {
    return device_.resolvePipeline(handle);
}
VkDescriptorSet VulkanSwapchain::resolveBindGroup(BindGroupHandle handle) const {
    return device_.resolveBindGroup(handle);
}

} // namespace engine::rhi::vulkan
