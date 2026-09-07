#pragma once

#include "rhi/api/Image.h"

#include <vk_mem_alloc.h>

namespace engine::rhi::vulkan {

class VulkanImage final : public IImage {
public:
    VulkanImage(VmaAllocator allocator,
                VkDevice device,
                VkExtent3D extent,
                VkFormat format,
                VkImageUsageFlags usage,
                VkImageAspectFlags aspectMask);
    ~VulkanImage() override;

    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&&) = delete;
    VulkanImage& operator=(VulkanImage&&) = delete;

    [[nodiscard]] VkImage handle() const { return image_; }
    [[nodiscard]] VkImageView view() const { return view_; }
    [[nodiscard]] VkFormat nativeFormat() const { return nativeFormat_; }
    [[nodiscard]] std::uint32_t width() const override { return extent_.width; }
    [[nodiscard]] std::uint32_t height() const override { return extent_.height; }
    [[nodiscard]] std::uint32_t depth() const override { return extent_.depth; }
    [[nodiscard]] TextureFormat format() const override { return format_; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkImageView view_{VK_NULL_HANDLE};
    VkFormat nativeFormat_{VK_FORMAT_UNDEFINED};
    TextureFormat format_{TextureFormat::Undefined};
    VkExtent3D extent_{};
};

} // namespace engine::rhi::vulkan
