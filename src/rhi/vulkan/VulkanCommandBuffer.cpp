#include "rhi/vulkan/VulkanCommandBuffer.h"

#include "core/logging/Log.h"

#include "core/base/BuildConfig.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanSynchronization.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace engine::rhi::vulkan {
namespace {

void toVulkanBlend(BlendMode mode, VkColorBlendEquationEXT& equation, VkBool32& enable) {
    if (mode == BlendMode::Off) {
        enable = VK_FALSE;
        return;
    }
    enable = VK_TRUE;
    equation.colorBlendOp = VK_BLEND_OP_ADD;
    equation.alphaBlendOp = VK_BLEND_OP_ADD;
    equation.dstColorBlendFactor =
        mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    equation.srcColorBlendFactor =
        mode == BlendMode::Alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
    equation.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    equation.dstAlphaBlendFactor =
        mode == BlendMode::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}

VkClearColorValue toClearColor(PixelFormat format, const ColorClearValue& clear) {
    VkClearColorValue result{};
    const auto numeric = formatInfo(format).numericType;
    if (const auto* value = std::get_if<std::array<std::uint32_t, 4>>(&clear.value)) {
        if (numeric != FormatNumericType::Uint)
            Log::fatal("VulkanCommandBuffer", "Unsigned clear value requires an unsigned integer attachment");
        std::copy(value->begin(), value->end(), result.uint32);
    } else {
        if (!isColorFormat(format) || numeric == FormatNumericType::Uint || numeric == FormatNumericType::Sint)
            Log::fatal("VulkanCommandBuffer", "Float clear value requires a floating-point or normalized attachment");
        const auto& floating = std::get<math::Vec4>(clear.value);
        result.float32[0] = floating.x; result.float32[1] = floating.y;
        result.float32[2] = floating.z; result.float32[3] = floating.w;
    }
    return result;
}

VkImageSubresourceLayers imageSubresource(PixelFormat format, std::uint32_t mipLevel,
                                         std::uint32_t arrayLayer) {
    if (!formatInfo(format).valid() || hasStencil(format))
        Log::fatal("VulkanCommandBuffer", "Unsupported image copy format");
    VkImageSubresourceLayers subresource{};
    subresource.aspectMask = isDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.mipLevel = mipLevel;
    subresource.baseArrayLayer = arrayLayer;
    subresource.layerCount = 1;
    return subresource;
}

VkOffset3D toVulkan(const Offset3D& offset) {
    return {static_cast<std::int32_t>(offset.x),
            static_cast<std::int32_t>(offset.y),
            static_cast<std::int32_t>(offset.z)};
}

VkExtent3D toVulkan(const Extent3D& extent) {
    return {extent.width, extent.height, extent.depth};
}

} // namespace

VulkanCommandBuffer::VulkanCommandBuffer(VkCommandBuffer commandBuffer,
                                         VulkanDevice& device,
                                         bool owned)
    : commandBuffer_(commandBuffer), device_(device), owned_(owned) {
    pfnSetColorBlendEnable_ = reinterpret_cast<PFN_vkCmdSetColorBlendEnableEXT>(
        vkGetDeviceProcAddr(device.device(), "vkCmdSetColorBlendEnableEXT"));
    pfnSetColorBlendEquation_ = reinterpret_cast<PFN_vkCmdSetColorBlendEquationEXT>(
        vkGetDeviceProcAddr(device.device(), "vkCmdSetColorBlendEquationEXT"));
    pfnSetColorWriteMask_ = reinterpret_cast<PFN_vkCmdSetColorWriteMaskEXT>(
        vkGetDeviceProcAddr(device.device(), "vkCmdSetColorWriteMaskEXT"));
    pfnSetPolygonMode_ = reinterpret_cast<PFN_vkCmdSetPolygonModeEXT>(
        vkGetDeviceProcAddr(device.device(), "vkCmdSetPolygonModeEXT"));
}

VulkanCommandBuffer::~VulkanCommandBuffer() {
    if (owned_ && commandBuffer_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_.device(), device_.commandPool(), 1, &commandBuffer_);
    }
    device_.discardTransientBufferUsage(commandBuffer_);
}

void VulkanCommandBuffer::begin() {
    if (state_ == CommandState::Recording) {
        Log::fatal("VulkanCommandBuffer",
                   "begin() requires an Initial or Executable command buffer");
    }
    // Re-beginning an Executable buffer resets it for another pass (frame buffers
    // are pooled and reused). The buffer must not be pending execution; callers
    // wait on its fence first (e.g. swapchain beginFrame).
    if (state_ == CommandState::Executable) {
        if (vkResetCommandBuffer(commandBuffer_, 0) != VK_SUCCESS) {
            Log::fatal("VulkanCommandBuffer", "vkResetCommandBuffer failed");
        }
        device_.discardTransientBufferUsage(commandBuffer_);
    }
    boundPipelineLayout_ = VK_NULL_HANDLE;
    dynamicRendering_ = false;
    activeRenderPass_ = {};
    activeFramebuffer_ = {};
    activeSubpass_ = 0;
    blendingEnabled_ = false;
    attachmentBlendingSupported_ = true;
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS) {
        Log::fatal("VulkanCommandBuffer", "vkBeginCommandBuffer failed");
    }
    state_ = CommandState::Recording;
}

void VulkanCommandBuffer::end() {
    if (dynamicRendering_ || activeRenderPass_)
        Log::fatal("VulkanCommandBuffer", "Rendering must end before command buffer end");
    if (state_ != CommandState::Recording) {
        Log::fatal("VulkanCommandBuffer", "end() requires a Recording command buffer");
    }
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        Log::fatal("VulkanCommandBuffer", "vkEndCommandBuffer failed");
    }
    state_ = CommandState::Executable;
}

void VulkanCommandBuffer::discard() {
    if (vkResetCommandBuffer(commandBuffer_, 0) != VK_SUCCESS)
        Log::fatal("VulkanCommandBuffer", "vkResetCommandBuffer(discard) failed");
    device_.discardTransientBufferUsage(commandBuffer_);
    state_ = CommandState::Initial;
    boundPipelineLayout_ = VK_NULL_HANDLE;
    dynamicRendering_ = false;
    activeRenderPass_ = {};
    activeFramebuffer_ = {};
    activeSubpass_ = 0;
    blendingEnabled_ = false;
    attachmentBlendingSupported_ = true;
}

void VulkanCommandBuffer::ensureRecording(const char* operation) {
    if (state_ != CommandState::Recording) {
        Log::fatal("VulkanCommandBuffer",
                   std::string(operation) + " requires a Recording command buffer");
    }
}

bool VulkanCommandBuffer::viewSupportsBlending(RID view) const {
    const auto info = device_.texture_view_info(view);
    const auto numeric = formatInfo(info.format).numericType;
    return numeric != FormatNumericType::Uint && numeric != FormatNumericType::Sint &&
        device_.texture_format_capabilities(info.format, device_.texture(info.texture).tiling()).colorAttachmentBlend;
}

bool VulkanCommandBuffer::subpassSupportsBlending(RID pass, RID framebuffer, std::uint32_t subpass) const {
    const auto& views = device_.framebuffer(framebuffer).desc().views;
    for (const auto& color : device_.renderPass(pass).desc().subpasses[subpass].colors) {
        if (color.attachment != kUnusedAttachment && !viewSupportsBlending(views[color.attachment])) return false;
    }
    return true;
}

void VulkanCommandBuffer::validateDraw(const char* operation) {
    ensureRecording(operation);
    if (blendingEnabled_ && !attachmentBlendingSupported_)
        Log::fatal("VulkanCommandBuffer", "Blending is unsupported for the active color attachments");
}

void VulkanCommandBuffer::resourceBarriers(std::span<const TextureBarrier> barriers) {
    if (barriers.empty()) {
        return;
    }
    ensureRecording("resourceBarriers");
    if (dynamicRendering_ || activeRenderPass_)
        Log::fatal("VulkanCommandBuffer", "Resource barriers must be outside rendering");
    std::vector<VkImageMemoryBarrier> imageBarriers;
    imageBarriers.reserve(barriers.size());
    VkPipelineStageFlags sourceStages{};
    VkPipelineStageFlags destinationStages{};
    for (const TextureBarrier& barrier : barriers) {
        VulkanState before = mapState(barrier.before);
        VulkanState after = mapState(barrier.after);
        // Swapchain acquisition waits at color output. Even a discarded image must
        // wait for that semaphore before its layout transition can write the image.
        if (barrier.before == ResourceState::Undefined && barrier.after == ResourceState::ColorAttachment)
            before.stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        // A stored attachment can finish in input-attachment layout. Its implicit
        // render-pass store is still a color/depth write, so include those scopes.
        if (barrier.before == ResourceState::InputAttachment &&
            barrier.sourceAccess == AccessMode::ReadWrite) {
            before.stage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            before.access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        if (barrier.after == ResourceState::InputAttachment &&
            barrier.destinationAccess == AccessMode::ReadWrite) {
            after.stage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            after.access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        VkImageMemoryBarrier native{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        native.srcAccessMask = filterAccess(before.access, barrier.sourceAccess);
        native.dstAccessMask = filterAccess(after.access, barrier.destinationAccess);
        native.oldLayout = before.layout;
        native.newLayout = after.layout;
        native.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        native.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        native.image = device_.resolveTexture(barrier.texture);
        native.subresourceRange.aspectMask = barrier.aspect == TextureAspect::Color
                                                 ? VK_IMAGE_ASPECT_COLOR_BIT
                                                 : VK_IMAGE_ASPECT_DEPTH_BIT;
        native.subresourceRange.baseMipLevel = barrier.baseMipLevel;
        native.subresourceRange.levelCount = barrier.mipCount;
        native.subresourceRange.baseArrayLayer = barrier.baseArrayLayer;
        native.subresourceRange.layerCount = barrier.layerCount;
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

void VulkanCommandBuffer::beginRendering(const RenderingInfo& info) {
    ensureRecording("beginRendering");
    if (dynamicRendering_ || activeRenderPass_)
        Log::fatal("VulkanCommandBuffer", "Rendering scopes cannot nest");
    std::vector<VkRenderingAttachmentInfo> colors;
    colors.reserve(info.colorAttachments.size());
    bool blendSupported = true;
    for (const ColorAttachment& attachment : info.colorAttachments) {
        VkRenderingAttachmentInfo native{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        native.imageView = device_.resolveTextureView(attachment.view);
        native.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        native.loadOp = mapLoadOp(attachment.loadOp);
        native.storeOp = mapStoreOp(attachment.storeOp);
        if (attachment.loadOp == LoadOp::Clear)
            native.clearValue.color = toClearColor(device_.texture_view_info(attachment.view).format, attachment.clearColor);
        blendSupported &= viewSupportsBlending(attachment.view);
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
        Log::fatal("VulkanCommandBuffer", "RHI supports at most one depth attachment per pass");
    }
    VkRenderingInfo native{VK_STRUCTURE_TYPE_RENDERING_INFO};
    native.renderArea.offset = {info.renderArea.x, info.renderArea.y};
    native.renderArea.extent = {info.renderArea.width, info.renderArea.height};
    native.layerCount = 1;
    native.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
    native.pColorAttachments = colors.data();
    native.pDepthAttachment = depths.empty() ? nullptr : depths.data();
    vkCmdBeginRendering(commandBuffer_, &native);
    dynamicRendering_ = true;
    attachmentBlendingSupported_ = blendSupported;
}

void VulkanCommandBuffer::endRendering() {
    ensureRecording("endRendering");
    if (!dynamicRendering_) Log::fatal("VulkanCommandBuffer", "No dynamic rendering scope");
    vkCmdEndRendering(commandBuffer_);
    dynamicRendering_ = false;
    attachmentBlendingSupported_ = true;
}

void VulkanCommandBuffer::beginRenderPass(const RenderPassBeginInfo& info) {
    ensureRecording("beginRenderPass");
    if (dynamicRendering_ || activeRenderPass_)
        Log::fatal("VulkanCommandBuffer", "Rendering scopes cannot nest");
    const auto& pass = device_.renderPass(info.renderPass);
    const auto& fb = device_.framebuffer(info.framebuffer);
    if (fb.desc().renderPass != info.renderPass ||
        info.clearValues.size() != pass.desc().attachments.size() ||
        info.renderArea.x < 0 || info.renderArea.y < 0 ||
        static_cast<std::uint64_t>(info.renderArea.x) + info.renderArea.width > fb.desc().width ||
        static_cast<std::uint64_t>(info.renderArea.y) + info.renderArea.height > fb.desc().height)
        Log::fatal("VulkanCommandBuffer", "Invalid render pass begin info");
    std::vector<VkClearValue> clears(info.clearValues.size());
    for (std::size_t i = 0; i < clears.size(); ++i) {
        if (isDepthFormat(pass.desc().attachments[i].format))
            clears[i].depthStencil = {info.clearValues[i].depth, 0};
        else if (pass.desc().attachments[i].loadOp == LoadOp::Clear) {
            clears[i].color = toClearColor(pass.desc().attachments[i].format, info.clearValues[i].color);
        }
    }
    VkRenderPassBeginInfo native{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    native.renderPass = pass.handle();
    native.framebuffer = fb.handle();
    native.renderArea = {{info.renderArea.x, info.renderArea.y},
                         {info.renderArea.width, info.renderArea.height}};
    native.clearValueCount = static_cast<std::uint32_t>(clears.size());
    native.pClearValues = clears.data();
    vkCmdBeginRenderPass(commandBuffer_, &native, VK_SUBPASS_CONTENTS_INLINE);
    activeRenderPass_ = info.renderPass;
    activeFramebuffer_ = info.framebuffer;
    attachmentBlendingSupported_ = subpassSupportsBlending(info.renderPass, info.framebuffer, 0);
    activeSubpass_ = 0;
    boundPipelineLayout_ = VK_NULL_HANDLE;
}

void VulkanCommandBuffer::nextSubpass() {
    ensureRecording("nextSubpass");
    if (!activeRenderPass_ || activeSubpass_ + 1 >= device_.renderPass(activeRenderPass_).desc().subpasses.size())
        Log::fatal("VulkanCommandBuffer", "No next subpass");
    vkCmdNextSubpass(commandBuffer_, VK_SUBPASS_CONTENTS_INLINE);
    ++activeSubpass_;
    attachmentBlendingSupported_ = subpassSupportsBlending(activeRenderPass_, activeFramebuffer_, activeSubpass_);
    boundPipelineLayout_ = VK_NULL_HANDLE;
}

void VulkanCommandBuffer::endRenderPass() {
    ensureRecording("endRenderPass");
    if (!activeRenderPass_ || activeSubpass_ + 1 != device_.renderPass(activeRenderPass_).desc().subpasses.size())
        Log::fatal("VulkanCommandBuffer", "Render pass must end in its last subpass");
    vkCmdEndRenderPass(commandBuffer_);
    activeRenderPass_ = {};
    activeFramebuffer_ = {};
    attachmentBlendingSupported_ = true;
    boundPipelineLayout_ = VK_NULL_HANDLE;
}

void VulkanCommandBuffer::setViewport(const Viewport& viewport) {
    ensureRecording("setViewport");
    const VkViewport native{viewport.x,
                            viewport.y,
                            viewport.width,
                            viewport.height,
                            viewport.minDepth,
                            viewport.maxDepth};
    vkCmdSetViewport(commandBuffer_, 0, 1, &native);
}

void VulkanCommandBuffer::setScissor(const Rect& scissor) {
    ensureRecording("setScissor");
    const VkRect2D native{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &native);
}

void VulkanCommandBuffer::setCullMode(CullMode mode) {
    ensureRecording("setCullMode");
    vkCmdSetCullMode(commandBuffer_, toVulkan(mode));
}

void VulkanCommandBuffer::setFrontFace(FrontFace face) {
    ensureRecording("setFrontFace");
    vkCmdSetFrontFace(commandBuffer_, toVulkan(face));
}

void VulkanCommandBuffer::setDepthTestEnable(bool enable) {
    ensureRecording("setDepthTestEnable");
    vkCmdSetDepthTestEnable(commandBuffer_, enable ? VK_TRUE : VK_FALSE);
}

void VulkanCommandBuffer::setDepthWriteEnable(bool enable) {
    ensureRecording("setDepthWriteEnable");
    vkCmdSetDepthWriteEnable(commandBuffer_, enable ? VK_TRUE : VK_FALSE);
}

void VulkanCommandBuffer::setDepthCompareOp(CompareOp compare) {
    ensureRecording("setDepthCompareOp");
    vkCmdSetDepthCompareOp(commandBuffer_, toVulkan(compare));
}

void VulkanCommandBuffer::setBlendState(BlendMode mode) {
    ensureRecording("setBlendState");
    blendingEnabled_ = mode != BlendMode::Off;
    // These states are declared dynamic even for depth-only pipelines. Set benign
    // defaults for slot zero so validation sees them initialized in every draw.
    const auto count = std::max(1U, colorAttachmentCount_);
    VkColorBlendEquationEXT equation{};
    VkBool32 enable = VK_FALSE;
    toVulkanBlend(mode, equation, enable);
    const std::vector<VkBool32> enables(count, enable);
    pfnSetColorBlendEnable_(commandBuffer_, 0, count, enables.data());
    // COLOR_BLEND_EQUATION_EXT is a pipeline dynamic state and must always be set
    // in the command buffer, even when blending is disabled.
    const std::vector<VkColorBlendEquationEXT> equations(count, equation);
    pfnSetColorBlendEquation_(commandBuffer_, 0, count, equations.data());
}

void VulkanCommandBuffer::setColorWriteMask(ColorWriteMask mask) {
    ensureRecording("setColorWriteMask");
    const auto count = std::max(1U, colorAttachmentCount_);
    const std::vector<VkColorComponentFlags> masks(count, toVulkan(mask));
    pfnSetColorWriteMask_(commandBuffer_, 0, count, masks.data());
}

void VulkanCommandBuffer::setPrimitiveTopology(PrimitiveTopology topology) {
    ensureRecording("setPrimitiveTopology");
    vkCmdSetPrimitiveTopology(commandBuffer_,
                              topology == PrimitiveTopology::TriangleList
                                  ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                  : VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
}

void VulkanCommandBuffer::setFillMode(FillMode mode) {
    ensureRecording("setFillMode");
    pfnSetPolygonMode_(commandBuffer_,
                       mode == FillMode::Solid ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE);
}

void VulkanCommandBuffer::bindPipeline(RID pipeline) {
    ensureRecording("bindPipeline");
    const ResolvedPipeline native = device_.resolvePipeline(pipeline);
    if ((activeRenderPass_ && (native.renderPass != activeRenderPass_ || native.subpass != activeSubpass_)) ||
        (dynamicRendering_ && native.renderPass))
        Log::fatal("VulkanCommandBuffer", "Pipeline is incompatible with current rendering scope");
    boundPipelineLayout_ = native.layout;
    colorAttachmentCount_ = native.colorAttachmentCount;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, native.pipeline);
}

void VulkanCommandBuffer::bindVertexBuffer(std::uint32_t slot,
                                           RID buffer,
                                           std::uint64_t offset) {
    ensureRecording("bindVertexBuffer");
    device_.recordTransientBufferUsage(buffer, commandBuffer_);
    const VkBuffer native = device_.resolveBuffer(buffer);
    const VkDeviceSize nativeOffset = offset;
    vkCmdBindVertexBuffers(commandBuffer_, slot, 1, &native, &nativeOffset);
}

void VulkanCommandBuffer::bindIndexBuffer(RID buffer,
                                          std::uint64_t offset,
                                          IndexFormat format) {
    ensureRecording("bindIndexBuffer");
    device_.recordTransientBufferUsage(buffer, commandBuffer_);
    vkCmdBindIndexBuffer(commandBuffer_,
                         device_.resolveBuffer(buffer),
                         offset,
                         format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandBuffer::bindDescriptorSet(std::uint32_t set, RID descriptorSet) {
    ensureRecording("bindDescriptorSet");
    if (boundPipelineLayout_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanCommandBuffer", "bindDescriptorSet requires a bound graphics pipeline");
    }
    const VkDescriptorSet descriptor = device_.resolveDescriptorSet(descriptorSet);
    vkCmdBindDescriptorSets(commandBuffer_,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            boundPipelineLayout_,
                            set,
                            1,
                            &descriptor,
                            0,
                            nullptr);
}

void VulkanCommandBuffer::draw(const DrawArguments& arguments) {
    validateDraw("draw");
    vkCmdDraw(commandBuffer_,
              arguments.vertexCount,
              arguments.instanceCount,
              arguments.firstVertex,
              arguments.firstInstance);
}

void VulkanCommandBuffer::drawIndexed(const DrawIndexedArguments& arguments) {
    validateDraw("drawIndexed");
    vkCmdDrawIndexed(commandBuffer_,
                     arguments.indexCount,
                     arguments.instanceCount,
                     arguments.firstIndex,
                     arguments.vertexOffset,
                     arguments.firstInstance);
}

void VulkanCommandBuffer::beginDebugLabel(std::string_view name, const math::Vec4& color) {
    ensureRecording("beginDebugLabel");
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

void VulkanCommandBuffer::endDebugLabel() {
    ensureRecording("endDebugLabel");
#if defined(MINI_DEBUG)
    const auto end = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(device_.device(), "vkCmdEndDebugUtilsLabelEXT"));
    if (end) {
        end(commandBuffer_);
    }
#endif
}

void VulkanCommandBuffer::copyBuffer(const BufferCopy& copy) {
    ensureRecording("copyBuffer");
    device_.recordTransientBufferUsage(copy.source, commandBuffer_);
    device_.recordTransientBufferUsage(copy.destination, commandBuffer_);
    const VkBufferCopy native{copy.sourceOffset, copy.destinationOffset, copy.size};
    vkCmdCopyBuffer(commandBuffer_,
                    device_.resolveBuffer(copy.source),
                    device_.resolveBuffer(copy.destination),
                    1,
                    &native);
}

void VulkanCommandBuffer::copyImage(const ImageCopy& copy) {
    ensureRecording("copyImage");
    // Callers are responsible for transitioning the images to CopySource/CopyDestination
    // states (via resourceBarriers) before recording the copy.
    VkImageCopy native{};
    native.srcSubresource = imageSubresource(device_.textureFormat(copy.source), copy.sourceMipLevel, copy.sourceArrayLayer);
    native.srcOffset = toVulkan(copy.sourceOffset);
    native.dstSubresource = imageSubresource(device_.textureFormat(copy.destination), copy.destinationMipLevel, copy.destinationArrayLayer);
    native.dstOffset = toVulkan(copy.destinationOffset);
    native.extent = toVulkan(copy.extent);
    vkCmdCopyImage(commandBuffer_,
                   device_.resolveTexture(copy.source),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   device_.resolveTexture(copy.destination),
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1,
                   &native);
}

void VulkanCommandBuffer::copyBufferToImage(const BufferImageCopy& copy) {
    ensureRecording("copyBufferToImage");
    device_.recordTransientBufferUsage(copy.buffer, commandBuffer_);
    VkBufferImageCopy native{};
    native.bufferOffset = copy.bufferOffset;
    native.bufferRowLength = copy.bufferRowLength;
    native.bufferImageHeight = copy.bufferImageHeight;
    native.imageSubresource = imageSubresource(device_.textureFormat(copy.texture), copy.mipLevel, copy.arrayLayer);
    native.imageOffset = toVulkan(copy.textureOffset);
    native.imageExtent = toVulkan(copy.extent);
    vkCmdCopyBufferToImage(commandBuffer_,
                           device_.resolveBuffer(copy.buffer),
                           device_.resolveTexture(copy.texture),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &native);
}

void VulkanCommandBuffer::copyImageToBuffer(const BufferImageCopy& copy) {
    ensureRecording("copyImageToBuffer");
    device_.recordTransientBufferUsage(copy.buffer, commandBuffer_);
    VkBufferImageCopy native{};
    native.bufferOffset = copy.bufferOffset;
    native.bufferRowLength = copy.bufferRowLength;
    native.bufferImageHeight = copy.bufferImageHeight;
    native.imageSubresource = imageSubresource(device_.textureFormat(copy.texture), copy.mipLevel, copy.arrayLayer);
    native.imageOffset = toVulkan(copy.textureOffset);
    native.imageExtent = toVulkan(copy.extent);
    vkCmdCopyImageToBuffer(commandBuffer_,
                           device_.resolveTexture(copy.texture),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           device_.resolveBuffer(copy.buffer),
                           1,
                           &native);
}

void VulkanCommandBuffer::updateBuffer(const BufferUpdate& update) {
    ensureRecording("updateBuffer");
    if (update.data.empty()) {
        return;
    }
    // Vulkan has no arbitrary-size in-command update, so stage the payload in a
    // host-visible scratch buffer owned by the device and copy from there.
    const RID staging = device_.acquireStagingBuffer(update.data.size_bytes(), commandBuffer_);
    device_.buffer_upload(staging, update.data);
    const BufferCopy copy{staging, update.destination, 0, update.offset, update.data.size_bytes()};
    copyBuffer(copy);
}

void VulkanCommandBuffer::updateImage(const ImageUpdate& update) {
    ensureRecording("updateImage");
    const auto& image = device_.texture(update.destination);
    if (update.mipLevel >= image.mipCount() || update.arrayLayer >= image.arrayLayers() ||
        image.samples() != SampleCount::One || !hasFlag(image.usage(), TextureUsage::TransferDestination) ||
        update.extent.depth != 1 || update.offset.z ||
        !update.extent.width || !update.extent.height)
        Log::fatal("VulkanCommandBuffer", "Invalid image update region");
    const auto width = std::max(1U, image.width() >> update.mipLevel);
    const auto height = std::max(1U, image.height() >> update.mipLevel);
    if (update.offset.x > width || update.offset.y > height ||
        update.extent.width > width - update.offset.x || update.extent.height > height - update.offset.y)
        Log::fatal("VulkanCommandBuffer", "Invalid image update region");
    std::size_t expectedSize{};
    if (!computeTextureByteSize(image.format(), update.extent.width, update.extent.height, 1, expectedSize) ||
        update.data.size_bytes() != expectedSize) {
        Log::fatal("VulkanCommandBuffer", "Image update data does not match the extent");
    }
    const RID staging = device_.acquireStagingBuffer(update.data.size_bytes(), commandBuffer_);
    device_.buffer_upload(staging, update.data);
    const BufferImageCopy copy{staging,
                               update.destination,
                               0,
                               0,
                               0,
                               update.mipLevel,
                               update.arrayLayer,
                               update.offset,
                               update.extent};
    copyBufferToImage(copy);
}

} // namespace engine::rhi::vulkan
