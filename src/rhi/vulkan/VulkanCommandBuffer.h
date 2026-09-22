#pragma once

#include "rhi/api/CommandBuffer.h"
#include "rhi/api/Device.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanDevice;

// Backend extension of ICommandBuffer exposing the raw VkCommandBuffer for adjacent
// tooling (e.g. editor UI overlays) that records into the same buffer outside of the
// RHI abstraction.
class IVulkanCommandBuffer : public ICommandBuffer {
public:
    [[nodiscard]] virtual VkCommandBuffer nativeCommandBuffer() const = 0;
};

class VulkanCommandBuffer final : public IVulkanCommandBuffer {
public:
    // owned: the wrapper frees the VkCommandBuffer on destruction. Buffers handed
    // out by VulkanDevice::createCommandBuffer are owned; swapchain frame buffers
    // are pooled by the swapchain and only wrapped.
    VulkanCommandBuffer(VkCommandBuffer commandBuffer,
                        VulkanDevice& device,
                        bool owned = false);
    ~VulkanCommandBuffer() override;

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;

    void begin() override;
    void end() override;
    [[nodiscard]] CommandState state() const override { return state_; }
    [[nodiscard]] VkCommandBuffer nativeCommandBuffer() const override {
        return commandBuffer_;
    }

    void resourceBarriers(std::span<const TextureBarrier> barriers) override;
    void beginRendering(const RenderingInfo& info) override;
    void endRendering() override;
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
    void bindPipeline(RID pipeline) override;
    void bindVertexBuffer(std::uint32_t slot, RID buffer, std::uint64_t offset) override;
    void bindIndexBuffer(RID buffer, std::uint64_t offset, IndexFormat format) override;
    void bindGroup(std::uint32_t set,
                   RID group,
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
    // Every record* entry point funnels through ensureRecording so an out-of-order
    // call fails fast instead of hitting Vulkan validation at submit time.
    void ensureRecording(const char* operation);

    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    VulkanDevice& device_;
    bool owned_{false};
    CommandState state_{CommandState::Initial};
    VkPipelineLayout boundPipelineLayout_{VK_NULL_HANDLE};
    PFN_vkCmdSetColorBlendEnableEXT pfnSetColorBlendEnable_{nullptr};
    PFN_vkCmdSetColorBlendEquationEXT pfnSetColorBlendEquation_{nullptr};
    PFN_vkCmdSetColorWriteMaskEXT pfnSetColorWriteMask_{nullptr};
    PFN_vkCmdSetPolygonModeEXT pfnSetPolygonMode_{nullptr};
};

} // namespace engine::rhi::vulkan
