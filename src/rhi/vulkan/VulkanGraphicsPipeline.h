#pragma once

#include "rhi/api/PipelineDesc.h"

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
                           VkPipelineCache cache);
    ~VulkanGraphicsPipeline();

    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline(VulkanGraphicsPipeline&&) = delete;
    VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&&) = delete;

    [[nodiscard]] VkPipeline handle() const { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const { return layout_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
};

} // namespace engine::rhi::vulkan
