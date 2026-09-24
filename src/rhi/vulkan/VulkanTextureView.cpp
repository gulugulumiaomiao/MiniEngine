#include "rhi/vulkan/VulkanTextureView.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanTexture.h"

#include <utility>

namespace engine::rhi::vulkan {
namespace {

VkFormat toVulkan(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case PixelFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case PixelFormat::Bgra8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case PixelFormat::Bgra8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case PixelFormat::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    case PixelFormat::Undefined: break;
    }
    Log::fatal("VulkanTextureView", "Unsupported texture view format");
}

VkComponentSwizzle toVulkan(SwizzleComponent component) {
    switch (component) {
    case SwizzleComponent::Identity: return VK_COMPONENT_SWIZZLE_IDENTITY;
    case SwizzleComponent::Zero: return VK_COMPONENT_SWIZZLE_ZERO;
    case SwizzleComponent::One: return VK_COMPONENT_SWIZZLE_ONE;
    case SwizzleComponent::R: return VK_COMPONENT_SWIZZLE_R;
    case SwizzleComponent::G: return VK_COMPONENT_SWIZZLE_G;
    case SwizzleComponent::B: return VK_COMPONENT_SWIZZLE_B;
    case SwizzleComponent::A: return VK_COMPONENT_SWIZZLE_A;
    }
    Log::fatal("VulkanTextureView", "Unsupported texture component swizzle");
}

} // namespace

VulkanTextureView::VulkanTextureView(VkDevice device, VulkanTexture& texture, TextureViewDesc desc)
    : IRHITextureView(&texture), device_(device), owned_(true) {
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
    : IRHITextureView(&texture), device_(device), view_(externalView), owned_(false) {
    if (view_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanTextureView", "Cannot wrap a null external texture view");
    }
}

VulkanTextureView::VulkanTextureView(VulkanTextureView&& other) noexcept
    : IRHITextureView(other.texture()),
      device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)),
      owned_(std::exchange(other.owned_, false)) {}

VulkanTextureView::~VulkanTextureView() {
    if (owned_ && view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
    }
}

} // namespace engine::rhi::vulkan
