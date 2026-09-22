#pragma once

#include "rhi/api/TextureView.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanTexture;

class VulkanTextureView final : public IRHITextureView {
public:
    VulkanTextureView(VkDevice device, VulkanTexture& texture, TextureViewDesc desc);
    // Wraps an externally owned view (for example a swapchain image view).
    VulkanTextureView(VkDevice device,
                      VulkanTexture& texture,
                      VkImageView externalView,
                      TextureViewDesc desc);
    ~VulkanTextureView() override;

    VulkanTextureView(const VulkanTextureView&) = delete;
    VulkanTextureView& operator=(const VulkanTextureView&) = delete;
    VulkanTextureView(VulkanTextureView&& other) noexcept;
    VulkanTextureView& operator=(VulkanTextureView&&) = delete;

    [[nodiscard]] VkImageView handle() const { return view_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkImageView view_{VK_NULL_HANDLE};
    bool owned_{};
};

} // namespace engine::rhi::vulkan
