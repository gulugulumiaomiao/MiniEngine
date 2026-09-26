#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::rhi::vulkan {

// Concrete Vulkan texture. Retains only native creation info (VkImage + the VkImageCreateInfo
// equivalents), not the rhi::TextureDesc; view creation/dedup lives in VulkanDevice, so this
// object no longer owns a default view or a per-texture view cache. Owns the full
// RHI -> Vulkan translation: callers hand it the TextureDesc and it derives the VkFormat,
// usage flags and sample count internally. Stored directly (by value) in the device's texture
// handle pool, so it is neither copyable nor movable.
class VulkanTexture final {
public:
    VulkanTexture(VmaAllocator allocator, const TextureDesc& desc);
    // Wraps an externally owned image (for example a swapchain image); no allocation happens.
    VulkanTexture(VkImage externalImage, const TextureDesc& desc);
    ~VulkanTexture();

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&&) = delete;
    VulkanTexture& operator=(VulkanTexture&&) = delete;

    [[nodiscard]] VkImage handle() const { return image_; }

    // Native-info accessors used by VulkanDevice (barriers, uploads, view normalization).
    [[nodiscard]] PixelFormat format() const { return format_; }
    [[nodiscard]] TextureType type() const { return type_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] std::uint32_t arrayLayers() const { return arrayLayers_; }
    [[nodiscard]] std::uint32_t mipCount() const { return mipLevels_; }

    [[nodiscard]] bool uploaded() const { return uploaded_; }
    void markUploaded() { uploaded_ = true; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    // VkImageCreateInfo-equivalent native info (replaces the retained rhi::TextureDesc).
    PixelFormat format_{PixelFormat::Undefined};
    TextureType type_{TextureType::Texture2D};
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint32_t arrayLayers_{1};
    std::uint32_t mipLevels_{};
    bool uploaded_{};
};

} // namespace engine::rhi::vulkan
