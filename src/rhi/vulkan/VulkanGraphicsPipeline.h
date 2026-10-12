#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanGraphicsPipeline final {
public:
    // The pipeline layout is shared through VulkanDevice's layout cache and the
    // pipeline cache outlives individual pipelines; neither is owned here.
    VulkanGraphicsPipeline(VkDevice device,
                           const GraphicsPipelineDesc& desc,
                           VkShaderModule vertexShader,
                           VkShaderModule fragmentShader,
                           VkPipelineLayout layout,
                           VkPipelineCache cache,
                           VkRenderPass renderPass = VK_NULL_HANDLE);
    ~VulkanGraphicsPipeline();

    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline(VulkanGraphicsPipeline&&) = delete;
    VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&&) = delete;

    [[nodiscard]] VkPipeline handle() const { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const { return layout_; }
    [[nodiscard]] RID renderPass() const { return renderPass_; }
    [[nodiscard]] std::uint32_t subpass() const { return subpass_; }
    [[nodiscard]] std::uint32_t colorAttachmentCount() const { return colorAttachmentCount_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    RID renderPass_;
    std::uint32_t subpass_{};
    std::uint32_t colorAttachmentCount_{};
};

} // namespace engine::rhi::vulkan
