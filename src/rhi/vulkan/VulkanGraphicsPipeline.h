#pragma once

#include "rhi/api/PipelineDesc.h"

#include <vulkan/vulkan.h>

#include <span>

namespace engine::rhi::vulkan {

class VulkanGraphicsPipeline final {
public:
    VulkanGraphicsPipeline(VkDevice device,
                           const GraphicsPipelineDesc& desc,
                           VkShaderModule vertexShader,
                           VkShaderModule fragmentShader,
                           std::span<const VkDescriptorSetLayout> descriptorLayouts);
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
