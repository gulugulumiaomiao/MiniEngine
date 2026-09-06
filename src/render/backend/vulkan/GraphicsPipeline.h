#pragma once

#include <vulkan/vulkan.h>

#include <string_view>

namespace engine {

struct RenderStateDesc;
struct VertexLayout;

class GraphicsPipeline final {
public:
    GraphicsPipeline(VkDevice device, VkFormat colorFormat,
                     VkDescriptorSetLayout sceneLayout,
                     VkDescriptorSetLayout materialLayout,
                     const RenderStateDesc& state,
                     const VertexLayout& vertexLayout,
                     VkShaderModule vertexShader,
                     std::string_view vertexEntry,
                     VkShaderModule fragmentShader,
                     std::string_view fragmentEntry);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    [[nodiscard]] VkPipeline handle() const { return pipeline_; }
    [[nodiscard]] VkPipelineLayout layout() const { return layout_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
};

} // namespace engine
