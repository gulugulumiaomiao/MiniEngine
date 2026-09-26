#include "rhi/vulkan/VulkanSwapchain.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <span>
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

} // namespace

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, const SwapchainDesc& desc)
    : device_(device), desc_(desc) {
    createFrameResources();
    create();
}

VulkanSwapchain::~VulkanSwapchain() {
    device_.waitIdle();
    commandBufferRid_ = {};
    destroy();
    for (const Frame& frame : frames_) {
        if (frame.commandBufferRid)
            device_.command_buffer_release_rid(frame.commandBufferRid);
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
    textureHandles_.clear();
    textureHandles_.reserve(images_.size());
    const TextureDesc externalDesc{.dimension = TextureType::Texture2D,
                                   .format = toRhi(surfaceFormat.format),
                                   .width = extent_.width,
                                   .height = extent_.height,
                                   .depth = 1,
                                   .arrayLayers = 1,
                                   .mipCount = 1,
                                   .usage = TextureUsage::ColorAttachment};
    for (VkImage image : images_)
        textureHandles_.push_back(device_.texture_allocate_rid(image, externalDesc));
    format_ = surfaceFormat.format;
    imageInitialized_.assign(imageCount, false);
    imageViews_.resize(imageCount);
    textureViewHandles_.clear();
    textureViewHandles_.reserve(imageCount);
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
        const TextureViewDesc viewDesc{.type = TextureType::Texture2D,
                                       .format = toRhi(format_),
                                       .baseMip = 0,
                                       .mipCount = 1,
                                       .baseLayer = 0,
                                       .layerCount = 1};
        textureViewHandles_.push_back(
            device_.texture_view_allocate_rid(textureHandles_[i], imageViews_[i], viewDesc));
        check(vkCreateSemaphore(device(), &semaphoreInfo, nullptr, &renderFinished_[i]),
              "vkCreateSemaphore(renderFinished)");
    }
}

void VulkanSwapchain::destroy() {
    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(device(), semaphore, nullptr);
    }
    renderFinished_.clear();
    for (RID handle : textureViewHandles_) {
        device_.texture_view_destroy(handle);
    }
    textureViewHandles_.clear();
    for (VkImageView view : imageViews_)
        vkDestroyImageView(device(), view, nullptr);
    imageViews_.clear();
    for (RID handle : textureHandles_) {
        device_.texture_destroy(handle);
    }
    textureHandles_.clear();
    images_.clear();
    imageInitialized_.clear();
    if (swapchain_ != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
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
        frames_[i].commandBufferRid =
            device_.command_buffer_allocate_rid(buffers[i], /*owned=*/false);
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
    // The fence we just waited on proves that this slot's previous submission finished,
    // so its command buffer staging buffers can be destroyed safely.
    device_.collectStagingBuffers();
    const VkResult acquire = vkAcquireNextImageKHR(
        device(), swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
        return FrameStatus::OutOfDate;
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        check(acquire, "vkAcquireNextImageKHR");
    }
    check(vkResetFences(device(), 1, &frame.inFlight), "vkResetFences");
    // The pooled VkCommandBuffer is registered in the device's command-buffer pool; begin()
    // resets and reopens it for this frame's recording.
    commandBufferRid_ = frame.commandBufferRid;
    VulkanCommandBuffer& cmd = device_.command_buffer(commandBufferRid_);
    cmd.begin();

    // On first use, transition from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    if (!imageInitialized_[imageIndex_]) {
        const TextureBarrier toColorAttachment{currentTexture(),
                                               TextureAspect::Color,
                                               ResourceState::Undefined,
                                               ResourceState::ColorAttachment,
                                               0,
                                               1,
                                               0,
                                               1};
        cmd.resourceBarriers(std::span{&toColorAttachment, 1});
    }

    frameOpen_ = true;
    return FrameStatus::Ready;
}

FrameStatus VulkanSwapchain::endFrame() {
    if (!frameOpen_)
        Log::fatal("VulkanSwapchain", "No frame is open");
    Frame& frame = frames_[currentFrame_];
    // Note: Layout transition to PRESENT_SRC_KHR is handled by RenderGraph
    device_.command_buffer(commandBufferRid_).end();
    // Command buffer staging buffers recorded this frame retire once this submission's
    // fence is signaled; tag them before submitting so collection sees the fence.
    device_.tagPendingStagingBuffers(frame.inFlight);
    device_.submit(commandBufferRid_,
                   SubmitSync{frame.imageAvailable,
                              renderFinished_[imageIndex_],
                              frame.inFlight,
                              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});
    commandBufferRid_ = {};
    const VkSemaphore finished = renderFinished_[imageIndex_];
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

RID VulkanSwapchain::commandBuffer() {
    if (!commandBufferRid_)
        Log::fatal("VulkanSwapchain", "No active command buffer");
    return commandBufferRid_;
}

RID VulkanSwapchain::currentTexture() const {
    return textureHandles_[imageIndex_];
}
RID VulkanSwapchain::currentTextureView() const {
    return textureViewHandles_[imageIndex_];
}
ResourceState VulkanSwapchain::currentTextureState() const {
    return imageInitialized_[imageIndex_] ? ResourceState::Present : ResourceState::Undefined;
}
PixelFormat VulkanSwapchain::format() const {
    return toRhi(format_);
}
VkDevice VulkanSwapchain::device() const {
    return device_.device();
}

} // namespace engine::rhi::vulkan
