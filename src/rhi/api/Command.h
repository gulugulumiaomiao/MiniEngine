#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <span>
#include <string_view>

// Command recording is expressed as free functions rather than an abstract command-buffer
// interface. Every function takes the RID of the command buffer it records into as its first
// argument; the definitions live in the concrete backend and resolve that RID through the
// device singleton (IDevice::active()) to the native command buffer instance. Layer 2 and the
// editor only ever pass RIDs around and never see a backend type, so this header stays free of
// any backend-native (for example Vulkan) declarations.
//
// Recording is explicit: begin() opens the buffer, commands record while it is open, end()
// seals it, and only a sealed buffer may be submitted. Submission is a backend/device concern
// and is not part of this recording surface.
namespace engine::rhi {

void begin(RID cmd);
void end(RID cmd);

void resourceBarriers(RID cmd, std::span<const TextureBarrier> barriers);
void beginRendering(RID cmd, const RenderingInfo& info);
void endRendering(RID cmd);
void setViewport(RID cmd, const Viewport& viewport);
void setScissor(RID cmd, const Rect& scissor);
void setCullMode(RID cmd, CullMode mode);
void setFrontFace(RID cmd, FrontFace face);
void setDepthTestEnable(RID cmd, bool enable);
void setDepthWriteEnable(RID cmd, bool enable);
void setDepthCompareOp(RID cmd, CompareOp compare);
void setBlendState(RID cmd, BlendMode mode);
void setColorWriteMask(RID cmd, ColorWriteMask mask);
void setPrimitiveTopology(RID cmd, PrimitiveTopology topology);
void setFillMode(RID cmd, FillMode mode);
void bindPipeline(RID cmd, RID pipeline);
void bindVertexBuffer(RID cmd, std::uint32_t slot, RID buffer, std::uint64_t offset = 0);
void bindIndexBuffer(RID cmd, RID buffer, std::uint64_t offset, IndexFormat format);
void bindGroup(RID cmd,
               std::uint32_t set,
               RID group,
               std::span<const std::uint32_t> dynamicOffsets = {});
void draw(RID cmd, const DrawArguments& arguments);
void drawIndexed(RID cmd, const DrawIndexedArguments& arguments);
void beginDebugLabel(RID cmd, std::string_view name, const math::Vec4& color);
void endDebugLabel(RID cmd);

// Transfer commands. The copy* family never transitions image layouts by itself: exactly
// like vkCmdCopy*/vkCmdUpdate*, the textures must already be in the CopySource/CopyDestination
// state (record resourceBarriers first, scoped to the mip level / array layer being copied).
// update* has no native arbitrary-size equivalent, so the backend stages the data in a scratch
// buffer and records a buffer-to-image copy instead.
void copyBuffer(RID cmd, const BufferCopy& copy);
void copyImage(RID cmd, const ImageCopy& copy);
void copyBufferToImage(RID cmd, const BufferImageCopy& copy);
void copyImageToBuffer(RID cmd, const BufferImageCopy& copy);
void updateBuffer(RID cmd, const BufferUpdate& update);
void updateImage(RID cmd, const ImageUpdate& update);

} // namespace engine::rhi
