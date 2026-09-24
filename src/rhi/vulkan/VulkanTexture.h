#pragma once

#include "rhi/api/Texture.h"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine::rhi::vulkan {

class VulkanDevice;

// Concrete Vulkan texture. Retains only native creation info (VkImage + the VkImageCreateInfo
// equivalents), not the rhi::TextureDesc; view creation/dedup lives in VulkanDevice, so this
// object no longer owns a default view or a per-texture view cache. The native-info accessors
// are concrete (backend-level), not part of the thinned IRHITexture interface.
class VulkanTexture final : public IRHITexture {
public:
    VulkanTexture(VulkanDevice& device,
                  VmaAllocator allocator,
                  const TextureDesc& desc,
                  VkFormat nativeFormat,
                  VkImageUsageFlags nativeUsage);
    VulkanTexture(VulkanDevice& device,
                  VkImage externalImage,
                  const TextureDesc& desc,
                  VkFormat nativeFormat);
    ~VulkanTexture() override;

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&&) = delete;
    VulkanTexture& operator=(VulkanTexture&&) = delete;

    [[nodiscard]] VkImage handle() const { return image_; }
    [[nodiscard]] VkFormat nativeFormat() const { return nativeFormat_; }
    [[nodiscard]] VkImageUsageFlags usage() const { return usage_; }

    // Native-info accessors used by VulkanDevice (barriers, uploads, view normalization).
    [[nodiscard]] PixelFormat format() const { return format_; }
    [[nodiscard]] TextureType type() const { return type_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] std::uint32_t depth() const { return depth_; }
    [[nodiscard]] std::uint32_t arrayLayers() const { return arrayLayers_; }
    [[nodiscard]] std::uint32_t mipCount() const { return mipLevels_; }

    [[nodiscard]] bool uploaded() const { return uploaded_; }
    void markUploaded() { uploaded_ = true; }

private:
    VulkanDevice* device_{};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkFormat nativeFormat_{VK_FORMAT_UNDEFINED};
    // VkImageCreateInfo-equivalent native info (replaces the retained rhi::TextureDesc).
    PixelFormat format_{PixelFormat::Undefined};
    TextureType type_{TextureType::Texture2D};
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint32_t depth_{1};
    std::uint32_t arrayLayers_{1};
    std::uint32_t mipLevels_{};
    VkImageUsageFlags usage_{};
    bool uploaded_{};
};

} // namespace engine::rhi::vulkan
