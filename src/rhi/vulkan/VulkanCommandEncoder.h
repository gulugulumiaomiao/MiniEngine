#pragma once

#include "rhi/CommandEncoder.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

struct ResolvedPipeline {
    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
};

class IVulkanResourceResolver {
public:
    virtual ~IVulkanResourceResolver() = default;
    [[nodiscard]] virtual VkDevice device() const = 0;
    [[nodiscard]] virtual VkBuffer resolveBuffer(BufferHandle handle) const = 0;
    [[nodiscard]] virtual VkImage resolveTexture(TextureHandle handle) const = 0;
    [[nodiscard]] virtual VkImageView resolveTextureView(TextureViewHandle handle) const = 0;
    [[nodiscard]] virtual ResolvedPipeline resolvePipeline(GraphicsPipelineHandle handle) const = 0;
    [[nodiscard]] virtual VkDescriptorSet resolveBindGroup(BindGroupHandle handle) const = 0;
};

class VulkanGraphicsCommandEncoder final : public IGraphicsCommandEncoder {
public:
    VulkanGraphicsCommandEncoder(VkCommandBuffer commandBuffer,
                                 const IVulkanResourceResolver& resources);

    void resourceBarriers(std::span<const TextureBarrier> barriers) override;
    void beginRendering(const RenderingInfo& info) override;
    void endRendering() override;
    void setViewport(const Viewport& viewport) override;
    void setScissor(const Rect& scissor) override;
    void bindPipeline(GraphicsPipelineHandle pipeline) override;
    void bindVertexBuffer(std::uint32_t slot, BufferHandle buffer,
                          std::uint64_t offset) override;
    void bindIndexBuffer(BufferHandle buffer, std::uint64_t offset,
                         IndexFormat format) override;
    void bindGroup(std::uint32_t set, BindGroupHandle group,
                   std::span<const std::uint32_t> dynamicOffsets) override;
    void draw(const DrawArguments& arguments) override;
    void drawIndexed(const DrawIndexedArguments& arguments) override;
    void beginDebugLabel(std::string_view name, const math::Vec4& color) override;
    void endDebugLabel() override;

private:
    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    const IVulkanResourceResolver& resources_;
    VkPipelineLayout boundPipelineLayout_{VK_NULL_HANDLE};
};

class VulkanTransferCommandEncoder final : public ITransferCommandEncoder {
public:
    VulkanTransferCommandEncoder(VkCommandBuffer commandBuffer,
                                 const IVulkanResourceResolver& resources);
    void copyBuffer(const BufferCopy& copy) override;

private:
    VkCommandBuffer commandBuffer_{VK_NULL_HANDLE};
    const IVulkanResourceResolver& resources_;
};

} // namespace engine::rhi::vulkan
