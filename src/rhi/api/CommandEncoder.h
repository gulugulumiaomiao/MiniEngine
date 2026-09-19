#pragma once

#include "rhi/api/RhiTypes.h"
#include "rhi/api/PipelineDesc.h"

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
    virtual void setDrawState(const DrawStateDesc&) {}
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
};

class ITransferCommandEncoder {
public:
    virtual ~ITransferCommandEncoder() = default;
    virtual void copyBuffer(const BufferCopy& copy) = 0;
};

} // namespace engine::rhi
