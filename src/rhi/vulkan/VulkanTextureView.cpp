#include "rhi/vulkan/VulkanTextureView.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanTexture.h"

#include <utility>

namespace engine::rhi::vulkan {

VulkanTextureView::VulkanTextureView(VkDevice device, VulkanTexture& texture, TextureViewDesc desc)
    : device_(device), owned_(true) {
    const PixelFormat format =
        desc.format == PixelFormat::Undefined ? texture.format() : desc.format;
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = texture.handle();
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = toVulkan(format);
    info.components = {toVulkan(desc.swizzle.r),
                       toVulkan(desc.swizzle.g),
                       toVulkan(desc.swizzle.b),
                       toVulkan(desc.swizzle.a)};
    info.subresourceRange.aspectMask =
        isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = desc.baseMip;
    info.subresourceRange.levelCount = desc.mipCount;
    info.subresourceRange.baseArrayLayer = desc.baseLayer;
    info.subresourceRange.layerCount = desc.layerCount;
    if (vkCreateImageView(device_, &info, nullptr, &view_) != VK_SUCCESS) {
        Log::fatal("VulkanTextureView", "vkCreateImageView failed");
    }
}

VulkanTextureView::VulkanTextureView(VkDevice device,
                                     VulkanTexture& texture,
                                     VkImageView externalView,
                                     TextureViewDesc desc)
    : device_(device), view_(externalView), owned_(false) {
    if (view_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanTextureView", "Cannot wrap a null external texture view");
    }
}

VulkanTextureView::~VulkanTextureView() {
    if (owned_ && view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
    }
}

} // namespace engine::rhi::vulkan
