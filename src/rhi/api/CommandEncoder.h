#pragma once

#include "rhi/api/PipelineDesc.h"
#include "rhi/api/RhiTypes.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vulkan/vulkan.h>

namespace engine::rhi {

class IGraphicsCommandEncoder {
public:
    virtual ~IGraphicsCommandEncoder() = default;

    virtual void resourceBarriers(std::span<const TextureBarrier> barriers) = 0;
    virtual void beginRendering(const RenderingInfo& info) = 0;
    virtual void endRendering() = 0;
    // Raw Vulkan command buffer for adjacent tooling (e.g. editor UI overlays) that
    // records into the same buffer outside of the RHI abstraction.
    [[nodiscard]] virtual VkCommandBuffer nativeCommandBuffer() const = 0;
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const Rect& scissor) = 0;
    virtual void setCullMode(CullMode mode) = 0;
    virtual void setFrontFace(FrontFace face) = 0;
    virtual void setDepthTestEnable(bool enable) = 0;
    virtual void setDepthWriteEnable(bool enable) = 0;
    virtual void setDepthCompareOp(CompareOp compare) = 0;
    virtual void setBlendState(BlendMode mode) = 0;
    virtual void setColorWriteMask(ColorWriteMask mask) = 0;
    virtual void setPrimitiveTopology(PrimitiveTopology topology) = 0;
    virtual void setFillMode(FillMode mode) = 0;
    virtual void bindPipeline(GraphicsPipelineHandle pipeline) = 0;
    virtual void
    bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, std::uint64_t offset, IndexFormat format) = 0;
    virtual void bindGroup(std::uint32_t set,
                           BindGroupHandle group,
                           std::span<const std::uint32_t> dynamicOffsets = {}) = 0;
    virtual void draw(const DrawArguments& arguments) = 0;
    virtual void drawIndexed(const DrawIndexedArguments& arguments) = 0;
    virtual void beginDebugLabel(std::string_view name, const math::Vec4& color) = 0;
    virtual void endDebugLabel() = 0;

    // Transfer commands. The copy* family never transitions image layouts by itself:
    // exactly like vkCmdCopy*/vkCmdUpdate*, the textures must already be in the
    // CopySource/CopyDestination state (record resourceBarriers first, scoped to the
    // mip level / array layer being copied). Vulkan has no native updateImage, so
    // backends stage the data in a scratch buffer and record a buffer-to-image copy
    // instead.
    virtual void copyBuffer(const BufferCopy& copy) = 0;
    virtual void copyImage(const ImageCopy& copy) = 0;
    virtual void copyBufferToImage(const BufferImageCopy& copy) = 0;
    virtual void copyImageToBuffer(const BufferImageCopy& copy) = 0;
    virtual void updateBuffer(const BufferUpdate& update) = 0;
    virtual void updateImage(const ImageUpdate& update) = 0;
};

} // namespace engine::rhi
