#include "rhi/vulkan/VulkanCommandEncoder.h"

#include "core/Log.h"

#include "core/BuildConfig.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace engine::rhi::vulkan {
namespace {

struct VulkanState {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    VkImageLayout layout;
};

VulkanState mapState(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:
        return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED};
    case ResourceState::CopySource:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ResourceState::CopyDestination:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ResourceState::ShaderRead:
        return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case ResourceState::ColorAttachment:
        return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case ResourceState::DepthAttachment:
        return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case ResourceState::Present:
        return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    Log::fatal("VulkanCommandEncoder", "Unsupported RHI resource state");
}

VkAttachmentLoadOp mapLoadOp(LoadOp operation) {
    switch (operation) {
    case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    Log::fatal("VulkanCommandEncoder", "Unsupported RHI load operation");
}

VkAttachmentStoreOp mapStoreOp(StoreOp operation) {
    return operation == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                       : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

} // namespace

VulkanGraphicsCommandEncoder::VulkanGraphicsCommandEncoder(
    VkCommandBuffer commandBuffer, const IVulkanResourceResolver& resources)
    : commandBuffer_(commandBuffer), resources_(resources) {}

void VulkanGraphicsCommandEncoder::resourceBarriers(
    std::span<const TextureBarrier> barriers) {
    if (barriers.empty()) {
        return;
    }
    std::vector<VkImageMemoryBarrier> imageBarriers;
    imageBarriers.reserve(barriers.size());
    VkPipelineStageFlags sourceStages{};
    VkPipelineStageFlags destinationStages{};
    for (const TextureBarrier& barrier : barriers) {
        const VulkanState before = mapState(barrier.before);
        const VulkanState after = mapState(barrier.after);
        VkImageMemoryBarrier native{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        native.srcAccessMask = before.access;
        native.dstAccessMask = after.access;
        native.oldLayout = before.layout;
        native.newLayout = after.layout;
        native.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        native.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        native.image = resources_.resolveTexture(barrier.texture);
        native.subresourceRange.aspectMask = barrier.aspect == TextureAspect::Color
                                                 ? VK_IMAGE_ASPECT_COLOR_BIT
                                                 : VK_IMAGE_ASPECT_DEPTH_BIT;
        native.subresourceRange.levelCount = 1;
        native.subresourceRange.layerCount = 1;
        imageBarriers.push_back(native);
        sourceStages |= before.stage;
        destinationStages |= after.stage;
    }
    vkCmdPipelineBarrier(commandBuffer_, sourceStages, destinationStages, 0, 0, nullptr, 0,
                         nullptr, static_cast<std::uint32_t>(imageBarriers.size()),
                         imageBarriers.data());
}

void VulkanGraphicsCommandEncoder::beginRendering(const RenderingInfo& info) {
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(info.colorAttachments.size());
    for (const ColorAttachment& attachment : info.colorAttachments) {
        VkRenderingAttachmentInfo native{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        native.imageView = resources_.resolveTextureView(attachment.view);
        native.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        native.loadOp = mapLoadOp(attachment.loadOp);
        native.storeOp = mapStoreOp(attachment.storeOp);
        native.clearValue.color = {{attachment.clearColor.x, attachment.clearColor.y,
                                    attachment.clearColor.z, attachment.clearColor.w}};
        colors.push_back(native);
    }
    std::vector<VkRenderingAttachmentInfo> depths;
    depths.reserve(info.depthAttachments.size());
    for (const DepthAttachment& attachment : info.depthAttachments) {
        VkRenderingAttachmentInfo native{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        native.imageView = resources_.resolveTextureView(attachment.view);
        native.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        native.loadOp = mapLoadOp(attachment.loadOp);
        native.storeOp = mapStoreOp(attachment.storeOp);
        native.clearValue.depthStencil = {attachment.clearDepth, 0};
        depths.push_back(native);
    }
    if (depths.size() > 1) {
        Log::fatal("VulkanCommandEncoder",
                   "RHI supports at most one depth attachment per pass");
    }
    VkRenderingInfo native{VK_STRUCTURE_TYPE_RENDERING_INFO};
    native.renderArea.offset = {info.renderArea.x, info.renderArea.y};
    native.renderArea.extent = {info.renderArea.width, info.renderArea.height};
    native.layerCount = 1;
    native.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    native.pColorAttachments = colors.data();
    native.pDepthAttachment = depths.empty() ? nullptr : depths.data();
    vkCmdBeginRendering(commandBuffer_, &native);
}

void VulkanGraphicsCommandEncoder::endRendering() { vkCmdEndRendering(commandBuffer_); }

void VulkanGraphicsCommandEncoder::setViewport(const Viewport& viewport) {
    const VkViewport native{viewport.x, viewport.y, viewport.width, viewport.height,
                            viewport.minDepth, viewport.maxDepth};
    vkCmdSetViewport(commandBuffer_, 0, 1, &native);
}

void VulkanGraphicsCommandEncoder::setScissor(const Rect& scissor) {
    const VkRect2D native{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &native);
}

void VulkanGraphicsCommandEncoder::bindPipeline(GraphicsPipelineHandle pipeline) {
    const ResolvedPipeline native = resources_.resolvePipeline(pipeline);
    boundPipelineLayout_ = native.layout;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, native.pipeline);
}

void VulkanGraphicsCommandEncoder::bindVertexBuffer(std::uint32_t slot, BufferHandle buffer,
                                                     std::uint64_t offset) {
    const VkBuffer native = resources_.resolveBuffer(buffer);
    const VkDeviceSize nativeOffset = offset;
    vkCmdBindVertexBuffers(commandBuffer_, slot, 1, &native, &nativeOffset);
}

void VulkanGraphicsCommandEncoder::bindIndexBuffer(BufferHandle buffer, std::uint64_t offset,
                                                    IndexFormat format) {
    vkCmdBindIndexBuffer(commandBuffer_, resources_.resolveBuffer(buffer), offset,
                         format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
}

void VulkanGraphicsCommandEncoder::bindGroup(
    std::uint32_t set, BindGroupHandle group,
    std::span<const std::uint32_t> dynamicOffsets) {
    if (boundPipelineLayout_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanCommandEncoder",
                   "bindGroup requires a bound graphics pipeline");
    }
    const VkDescriptorSet descriptor = resources_.resolveBindGroup(group);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            boundPipelineLayout_, set, 1, &descriptor,
                            static_cast<std::uint32_t>(dynamicOffsets.size()),
                            dynamicOffsets.data());
}

void VulkanGraphicsCommandEncoder::draw(const DrawArguments& arguments) {
    vkCmdDraw(commandBuffer_, arguments.vertexCount, arguments.instanceCount,
              arguments.firstVertex, arguments.firstInstance);
}

void VulkanGraphicsCommandEncoder::drawIndexed(const DrawIndexedArguments& arguments) {
    vkCmdDrawIndexed(commandBuffer_, arguments.indexCount, arguments.instanceCount,
                     arguments.firstIndex, arguments.vertexOffset, arguments.firstInstance);
}

void VulkanGraphicsCommandEncoder::beginDebugLabel(std::string_view name,
                                                    const math::Vec4& color) {
#if defined(MINI_DEBUG)
    const auto begin = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(resources_.device(), "vkCmdBeginDebugUtilsLabelEXT"));
    if (begin) {
        const std::string ownedName{name};
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = ownedName.c_str();
        label.color[0] = color.x;
        label.color[1] = color.y;
        label.color[2] = color.z;
        label.color[3] = color.w;
        begin(commandBuffer_, &label);
    }
#else
    (void)name;
    (void)color;
#endif
}

void VulkanGraphicsCommandEncoder::endDebugLabel() {
#if defined(MINI_DEBUG)
    const auto end = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(resources_.device(), "vkCmdEndDebugUtilsLabelEXT"));
    if (end) {
        end(commandBuffer_);
    }
#endif
}

VulkanTransferCommandEncoder::VulkanTransferCommandEncoder(
    VkCommandBuffer commandBuffer, const IVulkanResourceResolver& resources)
    : commandBuffer_(commandBuffer), resources_(resources) {}

void VulkanTransferCommandEncoder::copyBuffer(const BufferCopy& copy) {
    const VkBufferCopy native{copy.sourceOffset, copy.destinationOffset, copy.size};
    vkCmdCopyBuffer(commandBuffer_, resources_.resolveBuffer(copy.source),
                    resources_.resolveBuffer(copy.destination), 1, &native);
}

} // namespace engine::rhi::vulkan
