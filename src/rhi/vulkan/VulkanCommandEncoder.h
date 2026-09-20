#pragma once

#include "rhi/api/CommandEncoder.h"
#include "rhi/api/Device.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanDevice;

class VulkanGraphicsCommandEncoder final : public IGraphicsCommandEncoder {
public:
    VulkanGraphicsCommandEncoder(VkCommandBuffer commandBuffer, VulkanDevice& device);

    void resourceBarriers(std::span<const TextureBarrier> barriers) override;
    void beginRendering(const RenderingInfo& info) override;
    void endRendering() override;
    [[nodiscard]] VkCommandBuffer nativeCommandBuffer() const override {
        return commandBuffer_;
    }
    void setViewport(const Viewport& viewport) override;
    void setScissor(const Rect& scissor) override;
    void setCullMode(CullMode mode) override;
    void setFrontFace(FrontFace face) override;
    void setDepthTestEnable(bool enable) override;
    void setDepthWriteEnable(bool enable) override;
    void setDepthCompareOp(CompareOp compare) override;
    void setBlendState(BlendMode mode) override;
    void setColorWriteMask(ColorWriteMask mask) override;
    void setPrimitiveTopology(PrimitiveTopology topology) override;
    void setFillMode(FillMode mode) override;
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

    void copyBuffer(const BufferCopy& copy) override;
    void copyImage(const ImageCopy& copy) override;
    void copyBufferToImage(const BufferImageCopy& copy) override;
    void copyImageToBuffer(const BufferImageCopy& copy) override;
    void updateBuffer(const BufferUpdate& update) override;
    void updateImage(const ImageUpdate& update) override;

private:
    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    VulkanDevice& device_;
    VkPipelineLayout boundPipelineLayout_{VK_NULL_HANDLE};
    PFN_vkCmdSetColorBlendEnableEXT pfnSetColorBlendEnable_{nullptr};
    PFN_vkCmdSetColorBlendEquationEXT pfnSetColorBlendEquation_{nullptr};
    PFN_vkCmdSetColorWriteMaskEXT pfnSetColorWriteMask_{nullptr};
    PFN_vkCmdSetPolygonModeEXT pfnSetPolygonMode_{nullptr};
};

} // namespace engine::rhi::vulkan
