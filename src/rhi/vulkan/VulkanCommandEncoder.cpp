#include "rhi/vulkan/VulkanCommandEncoder.h"

#include "core/logging/Log.h"

#include "core/base/BuildConfig.h"

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
        return {VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case ResourceState::CopyDestination:
        return {VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case ResourceState::ShaderRead:
        return {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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

VkCullModeFlags mapCullMode(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp mapCompareOp(CompareOp compare) {
    switch (compare) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkColorComponentFlags mapColorMask(ColorWriteMask mask) {
    VkColorComponentFlags result{};
    if (hasFlag(mask, ColorWriteMask::Red))
        result |= VK_COLOR_COMPONENT_R_BIT;
    if (hasFlag(mask, ColorWriteMask::Green))
        result |= VK_COLOR_COMPONENT_G_BIT;
    if (hasFlag(mask, ColorWriteMask::Blue))
        result |= VK_COLOR_COMPONENT_B_BIT;
    if (hasFlag(mask, ColorWriteMask::Alpha))
        result |= VK_COLOR_COMPONENT_A_BIT;
    return result;
}

VkColorBlendEquationEXT mapBlendEquation(BlendMode mode) {
    VkColorBlendEquationEXT result{};
    result.colorBlendOp = VK_BLEND_OP_ADD;
    result.alphaBlendOp = VK_BLEND_OP_ADD;
    result.srcColorBlendFactor =
        mode == BlendMode::Alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
    result.dstColorBlendFactor =
        mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE
                                    : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    result.dstAlphaBlendFactor =
        mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE
                                    : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return result;
}

} // namespace

VulkanGraphicsCommandEncoder::VulkanGraphicsCommandEncoder(VkCommandBuffer commandBuffer,
                                                           const IDevice& device)
    : commandBuffer_(commandBuffer), device_(device) {}

void VulkanGraphicsCommandEncoder::resourceBarriers(std::span<const TextureBarrier> barriers) {
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
        native.image = device_.resolveTexture(barrier.texture);
        native.subresourceRange.aspectMask = barrier.aspect == TextureAspect::Color
                                                 ? VK_IMAGE_ASPECT_COLOR_BIT
                                                 : VK_IMAGE_ASPECT_DEPTH_BIT;
        native.subresourceRange.levelCount = 1;
        native.subresourceRange.layerCount = 1;
        imageBarriers.push_back(native);
        sourceStages |= before.stage;
        destinationStages |= after.stage;
    }
    vkCmdPipelineBarrier(commandBuffer_,
                         sourceStages,
                         destinationStages,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         static_cast<std::uint32_t>(imageBarriers.size()),
                         imageBarriers.data());
}

void VulkanGraphicsCommandEncoder::beginRendering(const RenderingInfo& info) {
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(info.colorAttachments.size());
    for (const ColorAttachment& attachment : info.colorAttachments) {
        VkRenderingAttachmentInfo native{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        native.imageView = device_.resolveTextureView(attachment.view);
        native.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        native.loadOp = mapLoadOp(attachment.loadOp);
        native.storeOp = mapStoreOp(attachment.storeOp);
        native.clearValue.color = {{attachment.clearColor.x,
                                    attachment.clearColor.y,
                                    attachment.clearColor.z,
                                    attachment.clearColor.w}};
        colors.push_back(native);
    }
    std::vector<VkRenderingAttachmentInfo> depths;
    depths.reserve(info.depthAttachments.size());
    for (const DepthAttachment& attachment : info.depthAttachments) {
        VkRenderingAttachmentInfo native{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        native.imageView = device_.resolveTextureView(attachment.view);
        native.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        native.loadOp = mapLoadOp(attachment.loadOp);
        native.storeOp = mapStoreOp(attachment.storeOp);
        native.clearValue.depthStencil = {attachment.clearDepth, 0};
        depths.push_back(native);
    }
    if (depths.size() > 1) {
        Log::fatal("VulkanCommandEncoder", "RHI supports at most one depth attachment per pass");
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

void VulkanGraphicsCommandEncoder::endRendering() {
    vkCmdEndRendering(commandBuffer_);
}

void VulkanGraphicsCommandEncoder::setViewport(const Viewport& viewport) {
    const VkViewport native{viewport.x,
                            viewport.y,
                            viewport.width,
                            viewport.height,
                            viewport.minDepth,
                            viewport.maxDepth};
    vkCmdSetViewport(commandBuffer_, 0, 1, &native);
}

void VulkanGraphicsCommandEncoder::setScissor(const Rect& scissor) {
    const VkRect2D native{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &native);
}

void VulkanGraphicsCommandEncoder::setDrawState(const DrawStateDesc& state) {
    vkCmdSetCullMode(commandBuffer_, mapCullMode(state.raster.cull));
    vkCmdSetFrontFace(commandBuffer_, state.raster.frontFace == FrontFace::Clockwise
                                         ? VK_FRONT_FACE_CLOCKWISE
                                         : VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(commandBuffer_, state.depthStencil.depthTestEnable);
    vkCmdSetDepthWriteEnable(commandBuffer_, state.depthStencil.depthWriteEnable);
    vkCmdSetDepthCompareOp(commandBuffer_, mapCompareOp(state.depthStencil.depthCompare));

    if (state.colorAttachmentCount > 0) {
        const VkBool32 blendEnable = state.blend.mode == BlendMode::Off ? VK_FALSE : VK_TRUE;
        const VkColorBlendEquationEXT blendEquation = mapBlendEquation(state.blend.mode);
        const VkColorComponentFlags colorMask = mapColorMask(state.blend.colorWriteMask);
        const std::vector<VkBool32> blendEnables(state.colorAttachmentCount, blendEnable);
        const std::vector<VkColorBlendEquationEXT> blendEquations(state.colorAttachmentCount,
                                                                    blendEquation);
        const std::vector<VkColorComponentFlags> colorMasks(state.colorAttachmentCount, colorMask);
        const auto setBlendEnable = reinterpret_cast<PFN_vkCmdSetColorBlendEnableEXT>(
            vkGetDeviceProcAddr(device_.device(), "vkCmdSetColorBlendEnableEXT"));
        const auto setBlendEquation = reinterpret_cast<PFN_vkCmdSetColorBlendEquationEXT>(
            vkGetDeviceProcAddr(device_.device(), "vkCmdSetColorBlendEquationEXT"));
        const auto setColorWriteMask = reinterpret_cast<PFN_vkCmdSetColorWriteMaskEXT>(
            vkGetDeviceProcAddr(device_.device(), "vkCmdSetColorWriteMaskEXT"));
        if (!setBlendEnable || !setBlendEquation || !setColorWriteMask) {
            throw std::runtime_error("VK_EXT_extended_dynamic_state3 commands are unavailable");
        }
        setBlendEnable(commandBuffer_, 0, state.colorAttachmentCount, blendEnables.data());
        setBlendEquation(commandBuffer_, 0, state.colorAttachmentCount, blendEquations.data());
        setColorWriteMask(commandBuffer_, 0, state.colorAttachmentCount, colorMasks.data());
    }
}

void VulkanGraphicsCommandEncoder::bindPipeline(GraphicsPipelineHandle pipeline) {
    const ResolvedPipeline native = device_.resolvePipeline(pipeline);
    boundPipelineLayout_ = native.layout;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, native.pipeline);
}

void VulkanGraphicsCommandEncoder::bindVertexBuffer(std::uint32_t slot,
                                                    BufferHandle buffer,
                                                    std::uint64_t offset) {
    const VkBuffer native = device_.resolveBuffer(buffer);
    const VkDeviceSize nativeOffset = offset;
    vkCmdBindVertexBuffers(commandBuffer_, slot, 1, &native, &nativeOffset);
}

void VulkanGraphicsCommandEncoder::bindIndexBuffer(BufferHandle buffer,
                                                   std::uint64_t offset,
                                                   IndexFormat format) {
    vkCmdBindIndexBuffer(commandBuffer_,
                         device_.resolveBuffer(buffer),
                         offset,
                         format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
}

void VulkanGraphicsCommandEncoder::bindGroup(std::uint32_t set,
                                             BindGroupHandle group,
                                             std::span<const std::uint32_t> dynamicOffsets) {
    if (boundPipelineLayout_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanCommandEncoder", "bindGroup requires a bound graphics pipeline");
    }
    const VkDescriptorSet descriptor = device_.resolveBindGroup(group);
    vkCmdBindDescriptorSets(commandBuffer_,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            boundPipelineLayout_,
                            set,
                            1,
                            &descriptor,
                            static_cast<std::uint32_t>(dynamicOffsets.size()),
                            dynamicOffsets.data());
}

void VulkanGraphicsCommandEncoder::draw(const DrawArguments& arguments) {
    vkCmdDraw(commandBuffer_,
              arguments.vertexCount,
              arguments.instanceCount,
              arguments.firstVertex,
              arguments.firstInstance);
}

void VulkanGraphicsCommandEncoder::drawIndexed(const DrawIndexedArguments& arguments) {
    vkCmdDrawIndexed(commandBuffer_,
                     arguments.indexCount,
                     arguments.instanceCount,
                     arguments.firstIndex,
                     arguments.vertexOffset,
                     arguments.firstInstance);
}

void VulkanGraphicsCommandEncoder::beginDebugLabel(std::string_view name, const math::Vec4& color) {
#if defined(MINI_DEBUG)
    const auto begin = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(device_.device(), "vkCmdBeginDebugUtilsLabelEXT"));
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
        vkGetDeviceProcAddr(device_.device(), "vkCmdEndDebugUtilsLabelEXT"));
    if (end) {
        end(commandBuffer_);
    }
#endif
}

VulkanTransferCommandEncoder::VulkanTransferCommandEncoder(VkCommandBuffer commandBuffer,
                                                           const IDevice& device)
    : commandBuffer_(commandBuffer), device_(device) {}

void VulkanTransferCommandEncoder::copyBuffer(const BufferCopy& copy) {
    const VkBufferCopy native{copy.sourceOffset, copy.destinationOffset, copy.size};
    vkCmdCopyBuffer(commandBuffer_,
                    device_.resolveBuffer(copy.source),
                    device_.resolveBuffer(copy.destination),
                    1,
                    &native);
}

} // namespace engine::rhi::vulkan
