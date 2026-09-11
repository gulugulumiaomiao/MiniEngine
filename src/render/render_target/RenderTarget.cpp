#include "render/render_target/RenderTarget.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <utility>

namespace engine {

RenderTarget::RenderTarget(rhi::IDevice& device) : device_(device) {}

RenderTarget::~RenderTarget() {
    release();
}

bool RenderTarget::validate(const RenderTargetDesc& desc) const {
    if (desc.width == 0 || desc.height == 0 ||
        (desc.colorAttachments.empty() && !desc.depthAttachment)) {
        Log::error("RenderTarget", "A render target needs a non-zero extent and an attachment");
        return false;
    }
    for (const RenderTargetColorAttachmentDesc& color : desc.colorAttachments) {
        if (!rhi::isColorFormat(color.format) ||
            rhi::hasFlag(color.additionalUsage, rhi::TextureUsage::DepthStencilAttachment)) {
            Log::error("RenderTarget", "Invalid color attachment description");
            return false;
        }
    }
    if (desc.depthAttachment &&
        (!rhi::isDepthFormat(desc.depthAttachment->format) ||
         rhi::hasFlag(desc.depthAttachment->additionalUsage, rhi::TextureUsage::ColorAttachment) ||
         desc.depthAttachment->clearDepth < 0.0F || desc.depthAttachment->clearDepth > 1.0F)) {
        Log::error("RenderTarget", "Invalid depth attachment description");
        return false;
    }
    return true;
}

bool RenderTarget::createAttachment(rhi::TextureFormat format,
                                    rhi::TextureAspect aspect,
                                    rhi::TextureUsage usage,
                                    std::string debugName,
                                    Attachment& destination) {
    Attachment created;
    created.format = format;
    created.texture = device_.createTexture({
        .dimension = rhi::TextureDimension::Texture2D,
        .format = format,
        .width = desc_.width,
        .height = desc_.height,
        .depth = 1,
        .mipCount = 1,
        .usage = usage,
        .debugName = std::move(debugName),
    });
    if (!created.texture) {
        Log::error("RenderTarget", "The RHI did not create a render-target texture");
        return false;
    }
    created.view = device_.createTextureView({
        .texture = created.texture,
        .format = format,
        .aspect = aspect,
        .baseMipLevel = 0,
        .mipCount = 1,
    });
    if (!created.view) {
        device_.destroyTexture(created.texture);
        Log::error("RenderTarget", "The RHI did not create a render-target texture view");
        return false;
    }
    destination = created;
    return true;
}

bool RenderTarget::create(RenderTargetDesc desc) {
    if (!validate(desc)) {
        return false;
    }

    // createAttachment reads the selected extent from desc_. Preserve the old description until
    // all replacement resources have been built so a failed replacement leaves the target valid.
    RenderTargetDesc oldDesc = std::move(desc_);
    desc_ = desc;
    std::vector<Attachment> colors;
    std::optional<Attachment> depth;
    colors.reserve(desc.colorAttachments.size());
    for (std::size_t index = 0; index < desc.colorAttachments.size(); ++index) {
        const RenderTargetColorAttachmentDesc& color = desc.colorAttachments[index];
        Attachment attachment;
        const std::string suffix = ".Color" + std::to_string(index);
        if (!createAttachment(color.format,
                              rhi::TextureAspect::Color,
                              color.additionalUsage | rhi::TextureUsage::ColorAttachment,
                              desc.debugName + suffix,
                              attachment)) {
            releaseResources(colors, depth);
            desc_ = std::move(oldDesc);
            return false;
        }
        colors.push_back(attachment);
    }

    if (desc.depthAttachment) {
        Attachment attachment;
        if (!createAttachment(desc.depthAttachment->format,
                              rhi::TextureAspect::Depth,
                              desc.depthAttachment->additionalUsage |
                                  rhi::TextureUsage::DepthStencilAttachment,
                              desc.debugName + ".Depth",
                              attachment)) {
            releaseResources(colors, depth);
            desc_ = std::move(oldDesc);
            return false;
        }
        depth = attachment;
    }

    releaseResources(colors_, depth_);
    colors_ = std::move(colors);
    depth_ = std::move(depth);
    return true;
}

bool RenderTarget::resize(std::uint32_t width, std::uint32_t height) {
    if (!valid()) {
        Log::error("RenderTarget", "Cannot resize an uninitialized render target");
        return false;
    }
    if (width == desc_.width && height == desc_.height) {
        return true;
    }
    RenderTargetDesc resized = desc_;
    resized.width = width;
    resized.height = height;
    return create(std::move(resized));
}

void RenderTarget::releaseAttachment(Attachment& attachment) {
    if (attachment.view) {
        device_.destroyTextureView(attachment.view);
    }
    if (attachment.texture) {
        device_.destroyTexture(attachment.texture);
    }
    attachment = {};
}

void RenderTarget::releaseResources(std::vector<Attachment>& colors,
                                    std::optional<Attachment>& depth) {
    if (depth) {
        releaseAttachment(*depth);
        depth.reset();
    }
    for (Attachment& color : colors) {
        releaseAttachment(color);
    }
    colors.clear();
}

void RenderTarget::release() {
    releaseResources(colors_, depth_);
    desc_ = {};
}

bool RenderTarget::valid() const {
    return desc_.width != 0 && desc_.height != 0 && (!colors_.empty() || depth_.has_value());
}

const RenderTarget::Attachment& RenderTarget::requireColor(std::size_t index) const {
    if (index >= colors_.size()) {
        Log::fatal("RenderTarget", "Color attachment index is out of range");
    }
    return colors_[index];
}

RenderTarget::Attachment& RenderTarget::requireColor(std::size_t index) {
    return const_cast<Attachment&>(std::as_const(*this).requireColor(index));
}

const RenderTarget::Attachment& RenderTarget::requireDepth() const {
    if (!depth_) {
        Log::fatal("RenderTarget", "Render target has no depth attachment");
    }
    return *depth_;
}

RenderTarget::Attachment& RenderTarget::requireDepth() {
    return const_cast<Attachment&>(std::as_const(*this).requireDepth());
}

rhi::TextureHandle RenderTarget::colorTexture(std::size_t index) const {
    return requireColor(index).texture;
}

rhi::TextureViewHandle RenderTarget::colorView(std::size_t index) const {
    return requireColor(index).view;
}

rhi::TextureFormat RenderTarget::colorFormat(std::size_t index) const {
    return requireColor(index).format;
}

rhi::TextureHandle RenderTarget::depthTexture() const {
    return requireDepth().texture;
}

rhi::TextureViewHandle RenderTarget::depthView() const {
    return requireDepth().view;
}

rhi::TextureFormat RenderTarget::depthFormat() const {
    return requireDepth().format;
}

rhi::RenderingInfo RenderTarget::renderingInfo() const {
    if (!valid()) {
        Log::fatal("RenderTarget", "Cannot build rendering info for an invalid render target");
    }
    rhi::RenderingInfo result;
    result.renderArea = {0, 0, desc_.width, desc_.height};
    result.colorAttachments.reserve(colors_.size());
    for (std::size_t index = 0; index < colors_.size(); ++index) {
        const RenderTargetColorAttachmentDesc& description = desc_.colorAttachments[index];
        result.colorAttachments.push_back(
            {colors_[index].view, description.loadOp, description.storeOp, description.clearColor});
    }
    if (depth_) {
        const RenderTargetDepthAttachmentDesc& description = *desc_.depthAttachment;
        result.depthAttachments.push_back(
            {depth_->view, description.loadOp, description.storeOp, description.clearDepth});
    }
    return result;
}

void RenderTarget::import(RenderGraph& graph,
                          rhi::ResourceState colorFinalState,
                          rhi::ResourceState depthFinalState) {
    for (std::size_t index = 0; index < colors_.size(); ++index) {
        (void)importColor(graph, index, colorFinalState);
    }
    if (depth_) {
        (void)importDepth(graph, depthFinalState);
    }
}

RgTextureHandle RenderTarget::importColor(RenderGraph& graph,
                                          std::size_t index,
                                          rhi::ResourceState finalState) {
    Attachment& color = requireColor(index);
    return graph.importTexture({
        .texture = color.texture,
        .view = color.view,
        .initialState = color.state,
        .finalState = finalState,
        .aspect = rhi::TextureAspect::Color,
        .trackedState = &color.state,
    });
}

RgTextureHandle RenderTarget::importDepth(RenderGraph& graph, rhi::ResourceState finalState) {
    Attachment& depth = requireDepth();
    return graph.importTexture({
        .texture = depth.texture,
        .view = depth.view,
        .initialState = depth.state,
        .finalState = finalState,
        .aspect = rhi::TextureAspect::Depth,
        .trackedState = &depth.state,
    });
}

} // namespace engine
