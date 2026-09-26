#include "rhi/api/Command.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanCommandBuffer.h"
#include "rhi/vulkan/VulkanDevice.h"

// Definitions of the backend-agnostic RHI command free functions declared in rhi/api/Command.h.
// There is exactly one implementation (Vulkan): each function resolves the command-buffer RID
// through the device singleton to the concrete VulkanCommandBuffer and forwards to it. Layer 2
// and the editor link against these definitions but only ever include the Vulkan-free Command.h.
namespace engine::rhi {
namespace {

[[nodiscard]] vulkan::VulkanCommandBuffer& resolve(RID cmd) {
    IDevice* device = IDevice::active();
    if (!device) {
        Log::fatal("RhiCommand", "No active RHI device for command recording");
    }
    return static_cast<vulkan::VulkanDevice&>(*device).command_buffer(cmd);
}

} // namespace

void begin(RID cmd) { resolve(cmd).begin(); }
void end(RID cmd) { resolve(cmd).end(); }

void resourceBarriers(RID cmd, std::span<const TextureBarrier> barriers) {
    resolve(cmd).resourceBarriers(barriers);
}
void beginRendering(RID cmd, const RenderingInfo& info) { resolve(cmd).beginRendering(info); }
void endRendering(RID cmd) { resolve(cmd).endRendering(); }
void setViewport(RID cmd, const Viewport& viewport) { resolve(cmd).setViewport(viewport); }
void setScissor(RID cmd, const Rect& scissor) { resolve(cmd).setScissor(scissor); }
void setCullMode(RID cmd, CullMode mode) { resolve(cmd).setCullMode(mode); }
void setFrontFace(RID cmd, FrontFace face) { resolve(cmd).setFrontFace(face); }
void setDepthTestEnable(RID cmd, bool enable) { resolve(cmd).setDepthTestEnable(enable); }
void setDepthWriteEnable(RID cmd, bool enable) { resolve(cmd).setDepthWriteEnable(enable); }
void setDepthCompareOp(RID cmd, CompareOp compare) { resolve(cmd).setDepthCompareOp(compare); }
void setBlendState(RID cmd, BlendMode mode) { resolve(cmd).setBlendState(mode); }
void setColorWriteMask(RID cmd, ColorWriteMask mask) { resolve(cmd).setColorWriteMask(mask); }
void setPrimitiveTopology(RID cmd, PrimitiveTopology topology) {
    resolve(cmd).setPrimitiveTopology(topology);
}
void setFillMode(RID cmd, FillMode mode) { resolve(cmd).setFillMode(mode); }
void bindPipeline(RID cmd, RID pipeline) { resolve(cmd).bindPipeline(pipeline); }
void bindVertexBuffer(RID cmd, std::uint32_t slot, RID buffer, std::uint64_t offset) {
    resolve(cmd).bindVertexBuffer(slot, buffer, offset);
}
void bindIndexBuffer(RID cmd, RID buffer, std::uint64_t offset, IndexFormat format) {
    resolve(cmd).bindIndexBuffer(buffer, offset, format);
}
void bindGroup(RID cmd, std::uint32_t set, RID group, std::span<const std::uint32_t> dynamicOffsets) {
    resolve(cmd).bindGroup(set, group, dynamicOffsets);
}
void draw(RID cmd, const DrawArguments& arguments) { resolve(cmd).draw(arguments); }
void drawIndexed(RID cmd, const DrawIndexedArguments& arguments) {
    resolve(cmd).drawIndexed(arguments);
}
void beginDebugLabel(RID cmd, std::string_view name, const math::Vec4& color) {
    resolve(cmd).beginDebugLabel(name, color);
}
void endDebugLabel(RID cmd) { resolve(cmd).endDebugLabel(); }

void copyBuffer(RID cmd, const BufferCopy& copy) { resolve(cmd).copyBuffer(copy); }
void copyImage(RID cmd, const ImageCopy& copy) { resolve(cmd).copyImage(copy); }
void copyBufferToImage(RID cmd, const BufferImageCopy& copy) {
    resolve(cmd).copyBufferToImage(copy);
}
void copyImageToBuffer(RID cmd, const BufferImageCopy& copy) {
    resolve(cmd).copyImageToBuffer(copy);
}
void updateBuffer(RID cmd, const BufferUpdate& update) { resolve(cmd).updateBuffer(update); }
void updateImage(RID cmd, const ImageUpdate& update) { resolve(cmd).updateImage(update); }

} // namespace engine::rhi
