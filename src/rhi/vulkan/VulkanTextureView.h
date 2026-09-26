#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanTexture;

// Concrete Vulkan image view. Stored directly (by value) in the device's texture-view handle
// pool; view creation and dedup live in VulkanDevice, so this object keeps only its native
// VkImageView. Non-copyable and non-movable (the pool constructs it in place).
class VulkanTextureView final {
public:
    VulkanTextureView(VkDevice device, VulkanTexture& texture, TextureViewDesc desc);
    // Wraps an externally owned view (for example a swapchain image view).
    VulkanTextureView(VkDevice device,
                      VulkanTexture& texture,
                      VkImageView externalView,
                      TextureViewDesc desc);
    ~VulkanTextureView();

    VulkanTextureView(const VulkanTextureView&) = delete;
    VulkanTextureView& operator=(const VulkanTextureView&) = delete;
    VulkanTextureView(VulkanTextureView&&) = delete;
    VulkanTextureView& operator=(VulkanTextureView&&) = delete;

    [[nodiscard]] VkImageView handle() const { return view_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkImageView view_{VK_NULL_HANDLE};
    bool owned_{};
};

} // namespace engine::rhi::vulkan
