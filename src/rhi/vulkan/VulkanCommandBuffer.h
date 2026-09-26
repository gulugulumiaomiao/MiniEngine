#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace engine::rhi::vulkan {

class VulkanDevice;

// Recording lifecycle of a Vulkan command buffer. Backend-internal state (the RHI command
// free functions in Command.h do not expose it); submission requires the Executable state.
enum class CommandState {
    Initial,    // Allocated but recording has not begun.
    Recording,  // Between begin() and end().
    Executable, // Recording finished; ready to be submitted through VulkanDevice::submit.
};

// Concrete Vulkan command buffer and the single implementation of the RHI command surface. The
// backend-agnostic free functions in rhi/api/Command.h resolve a command-buffer RID through the
// device singleton and forward to these methods. Stored directly (by value) in the device's
// command-buffer handle pool, so it is non-copyable/non-movable.
//
// owned: the wrapper frees the VkCommandBuffer on destruction. Buffers created through
// VulkanDevice::command_buffer_create are owned; swapchain frame buffers are pooled by the
// swapchain and only wrapped (owned == false).
class VulkanCommandBuffer final {
public:
    VulkanCommandBuffer(VkCommandBuffer commandBuffer, VulkanDevice& device, bool owned = false);
    ~VulkanCommandBuffer();

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer(VulkanCommandBuffer&&) = delete;
    VulkanCommandBuffer& operator=(VulkanCommandBuffer&&) = delete;

    void begin();
    void end();
    [[nodiscard]] CommandState state() const { return state_; }
    [[nodiscard]] VkCommandBuffer nativeCommandBuffer() const { return commandBuffer_; }

    void resourceBarriers(std::span<const TextureBarrier> barriers);
    void beginRendering(const RenderingInfo& info);
    void endRendering();
    void setViewport(const Viewport& viewport);
    void setScissor(const Rect& scissor);
    void setCullMode(CullMode mode);
    void setFrontFace(FrontFace face);
    void setDepthTestEnable(bool enable);
    void setDepthWriteEnable(bool enable);
    void setDepthCompareOp(CompareOp compare);
    void setBlendState(BlendMode mode);
    void setColorWriteMask(ColorWriteMask mask);
    void setPrimitiveTopology(PrimitiveTopology topology);
    void setFillMode(FillMode mode);
    void bindPipeline(RID pipeline);
    void bindVertexBuffer(std::uint32_t slot, RID buffer, std::uint64_t offset = 0);
    void bindIndexBuffer(RID buffer, std::uint64_t offset, IndexFormat format);
    void
    bindGroup(std::uint32_t set, RID group, std::span<const std::uint32_t> dynamicOffsets = {});
    void draw(const DrawArguments& arguments);
    void drawIndexed(const DrawIndexedArguments& arguments);
    void beginDebugLabel(std::string_view name, const math::Vec4& color);
    void endDebugLabel();

    // Transfer commands. The copy* family never transitions image layouts by itself: exactly
    // like vkCmdCopy*/vkCmdUpdate*, the textures must already be in the CopySource/CopyDestination
    // state (record resourceBarriers first, scoped to the mip level / array layer being copied).
    void copyBuffer(const BufferCopy& copy);
    void copyImage(const ImageCopy& copy);
    void copyBufferToImage(const BufferImageCopy& copy);
    void copyImageToBuffer(const BufferImageCopy& copy);
    void updateBuffer(const BufferUpdate& update);
    void updateImage(const ImageUpdate& update);

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
