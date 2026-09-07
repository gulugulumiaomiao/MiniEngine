#pragma once

#include "rhi/api/CommandEncoder.h"
#include "rhi/api/Device.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanGraphicsCommandEncoder final : public IGraphicsCommandEncoder {
public:
    VulkanGraphicsCommandEncoder(VkCommandBuffer commandBuffer, const IDevice& device);

    void resourceBarriers(std::span<const TextureBarrier> barriers) override;
    void beginRendering(const RenderingInfo& info) override;
    void endRendering() override;
    void setViewport(const Viewport& viewport) override;
    void setScissor(const Rect& scissor) override;
    void bindPipeline(GraphicsPipelineHandle pipeline) override;
    void bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset) override;
    void bindIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) override;
    void bindGroup(std::uint32_t set,
                   BindGroupHandle group,
                   std::span<const std::uint32_t> dynamicOffsets) override;
    void draw(const DrawArguments& arguments) override;
    void drawIndexed(const DrawIndexedArguments& arguments) override;
    void beginDebugLabel(std::string_view name, const math::Vec4& color) override;
    void endDebugLabel() override;

private:
    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    const IDevice& device_;
    VkPipelineLayout boundPipelineLayout_{VK_NULL_HANDLE};
};

class VulkanTransferCommandEncoder final : public ITransferCommandEncoder {
public:
    VulkanTransferCommandEncoder(VkCommandBuffer commandBuffer, const IDevice& device);
    void copyBuffer(const BufferCopy& copy) override;

private:
    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    const IDevice& device_;
};

} // namespace engine::rhi::vulkan
