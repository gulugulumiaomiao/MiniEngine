#pragma once

#include "rhi/api/ResourceDesc.h"
#include <vulkan/vulkan.h>
#include <span>

namespace engine::rhi::vulkan {

class VulkanRenderPass final {
public:
    VulkanRenderPass(VkDevice device, RenderPassDesc desc);
    ~VulkanRenderPass();
    VulkanRenderPass(const VulkanRenderPass&) = delete;
    VulkanRenderPass& operator=(const VulkanRenderPass&) = delete;
    [[nodiscard]] VkRenderPass handle() const { return handle_; }
    [[nodiscard]] const RenderPassDesc& desc() const { return desc_; }
private:
    VkDevice device_;
    RenderPassDesc desc_;
    VkRenderPass handle_{VK_NULL_HANDLE};
};

class VulkanFramebuffer final {
public:
    VulkanFramebuffer(VkDevice device, FramebufferDesc desc, VkRenderPass renderPass,
                      std::span<const VkImageView> views);
    ~VulkanFramebuffer();
    VulkanFramebuffer(const VulkanFramebuffer&) = delete;
    VulkanFramebuffer& operator=(const VulkanFramebuffer&) = delete;
    [[nodiscard]] VkFramebuffer handle() const { return handle_; }
    [[nodiscard]] const FramebufferDesc& desc() const { return desc_; }
private:
    VkDevice device_;
    FramebufferDesc desc_;
    VkFramebuffer handle_{VK_NULL_HANDLE};
};

} // namespace engine::rhi::vulkan
