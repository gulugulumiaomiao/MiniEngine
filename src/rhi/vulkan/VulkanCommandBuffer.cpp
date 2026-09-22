#include "rhi/vulkan/VulkanCommandBuffer.h"

#include "core/logging/Log.h"

#include "core/base/BuildConfig.h"
#include "rhi/vulkan/VulkanDevice.h"

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
    Log::fatal("VulkanCommandBuffer", "Unsupported RHI resource state");
}

VkAttachmentLoadOp mapLoadOp(LoadOp operation) {
    switch (operation) {
    case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    Log::fatal("VulkanCommandBuffer", "Unsupported RHI load operation");
}

VkAttachmentStoreOp mapStoreOp(StoreOp operation) {
    return operation == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                       : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

VkCullModeFlags toVulkan(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkFrontFace toVulkan(FrontFace face) {
    return face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkCompareOp toVulkan(CompareOp compare) {
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

VkColorComponentFlags toVulkan(ColorWriteMask mask) {
    VkColorComponentFlags result = 0;
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

std::uint32_t bytesPerPixel(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8Unorm:
    case PixelFormat::Rgba8Srgb:
    case PixelFormat::Bgra8Unorm:
    case PixelFormat::Bgra8Srgb:
    case PixelFormat::Depth32Float: return 4U;
    case PixelFormat::Undefined: break;
    }
    Log::fatal("VulkanCommandBuffer", "Unsupported RHI texture format");
}

VkImageSubresourceLayers imageSubresource(std::uint32_t mipLevel, std::uint32_t arrayLayer) {
    VkImageSubresourceLayers subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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
    }
    boundPipelineLayout_ = VK_NULL_HANDLE;
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS) {
        Log::fatal("VulkanCommandBuffer", "vkBeginCommandBuffer failed");
    }
    state_ = CommandState::Recording;
}

void VulkanCommandBuffer::end() {
    if (state_ != CommandState::Recording) {
        Log::fatal("VulkanCommandBuffer", "end() requires a Recording command buffer");
    }
    if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
        Log::fatal("VulkanCommandBuffer", "vkEndCommandBuffer failed");
    }
    state_ = CommandState::Executable;
}

void VulkanCommandBuffer::ensureRecording(const char* operation) {
    if (state_ != CommandState::Recording) {
        Log::fatal("VulkanCommandBuffer",
                   std::string(operation) + " requires a Recording command buffer");
    }
}

void VulkanCommandBuffer::resourceBarriers(std::span<const TextureBarrier> barriers) {
    if (barriers.empty()) {
        return;
    }
    ensureRecording("resourceBarriers");
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
}

void VulkanCommandBuffer::endRendering() {
    ensureRecording("endRendering");
    vkCmdEndRendering(commandBuffer_);
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
    VkColorBlendEquationEXT equation{};
    VkBool32 enable = VK_FALSE;
    toVulkanBlend(mode, equation, enable);
    const VkBool32 enables[1] = {enable};
    pfnSetColorBlendEnable_(commandBuffer_, 0, 1, enables);
    // COLOR_BLEND_EQUATION_EXT is a pipeline dynamic state and must always be set
    // in the command buffer, even when blending is disabled.
    const VkColorBlendEquationEXT equations[1] = {equation};
    pfnSetColorBlendEquation_(commandBuffer_, 0, 1, equations);
}

void VulkanCommandBuffer::setColorWriteMask(ColorWriteMask mask) {
    ensureRecording("setColorWriteMask");
    const VkColorComponentFlags masks[1] = {toVulkan(mask)};
    pfnSetColorWriteMask_(commandBuffer_, 0, 1, masks);
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

void VulkanCommandBuffer::bindPipeline(GraphicsPipelineHandle pipeline) {
    ensureRecording("bindPipeline");
    const ResolvedPipeline native = device_.resolvePipeline(pipeline);
    boundPipelineLayout_ = native.layout;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, native.pipeline);
}

void VulkanCommandBuffer::bindVertexBuffer(std::uint32_t slot,
                                           BufferHandle buffer,
                                           std::uint64_t offset) {
    ensureRecording("bindVertexBuffer");
    const VkBuffer native = device_.resolveBuffer(buffer);
    const VkDeviceSize nativeOffset = offset;
    vkCmdBindVertexBuffers(commandBuffer_, slot, 1, &native, &nativeOffset);
}

void VulkanCommandBuffer::bindIndexBuffer(BufferHandle buffer,
                                          std::uint64_t offset,
                                          IndexFormat format) {
    ensureRecording("bindIndexBuffer");
    vkCmdBindIndexBuffer(commandBuffer_,
                         device_.resolveBuffer(buffer),
                         offset,
                         format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16
                                                       : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandBuffer::bindGroup(std::uint32_t set,
                                    BindGroupHandle group,
                                    std::span<const std::uint32_t> dynamicOffsets) {
    ensureRecording("bindGroup");
    if (boundPipelineLayout_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanCommandBuffer", "bindGroup requires a bound graphics pipeline");
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

void VulkanCommandBuffer::draw(const DrawArguments& arguments) {
    ensureRecording("draw");
    vkCmdDraw(commandBuffer_,
              arguments.vertexCount,
              arguments.instanceCount,
              arguments.firstVertex,
              arguments.firstInstance);
}

void VulkanCommandBuffer::drawIndexed(const DrawIndexedArguments& arguments) {
    ensureRecording("drawIndexed");
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
    native.srcSubresource = imageSubresource(copy.sourceMipLevel, copy.sourceArrayLayer);
    native.srcOffset = toVulkan(copy.sourceOffset);
    native.dstSubresource = imageSubresource(copy.destinationMipLevel, copy.destinationArrayLayer);
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
    VkBufferImageCopy native{};
    native.bufferOffset = copy.bufferOffset;
    native.bufferRowLength = copy.bufferRowLength;
    native.bufferImageHeight = copy.bufferImageHeight;
    native.imageSubresource = imageSubresource(copy.mipLevel, copy.arrayLayer);
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
    VkBufferImageCopy native{};
    native.bufferOffset = copy.bufferOffset;
    native.bufferRowLength = copy.bufferRowLength;
    native.bufferImageHeight = copy.bufferImageHeight;
    native.imageSubresource = imageSubresource(copy.mipLevel, copy.arrayLayer);
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
    const BufferHandle staging = device_.acquireStagingBuffer(update.data.size_bytes());
    device_.uploadBuffer(staging, update.data);
    const BufferCopy copy{staging, update.destination, 0, update.offset, update.data.size_bytes()};
    copyBuffer(copy);
}

void VulkanCommandBuffer::updateImage(const ImageUpdate& update) {
    ensureRecording("updateImage");
    if (update.extent.width == 0 || update.extent.height == 0 || update.extent.depth == 0) {
        Log::fatal("VulkanCommandBuffer", "Image update extent must not be empty");
    }
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(update.extent.width) *
                                     update.extent.height * update.extent.depth;
    const std::uint64_t expectedSize =
        pixelCount * bytesPerPixel(device_.textureFormat(update.destination));
    if (update.data.size_bytes() != expectedSize) {
        Log::fatal("VulkanCommandBuffer", "Image update data does not match the extent");
    }
    const BufferHandle staging = device_.acquireStagingBuffer(update.data.size_bytes());
    device_.uploadBuffer(staging, update.data);
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
