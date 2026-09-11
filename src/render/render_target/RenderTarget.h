#pragma once

#include "render/render_graph/RenderGraph.h"
#include "rhi/api/ResourceDesc.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

struct RenderTargetColorAttachmentDesc {
    rhi::TextureFormat format{rhi::TextureFormat::Rgba8Unorm};
    rhi::TextureUsage additionalUsage{rhi::TextureUsage::None};
    rhi::LoadOp loadOp{rhi::LoadOp::Clear};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    math::Vec4 clearColor{0.0F, 0.0F, 0.0F, 1.0F};
};

struct RenderTargetDepthAttachmentDesc {
    rhi::TextureFormat format{rhi::TextureFormat::Depth32Float};
    rhi::TextureUsage additionalUsage{rhi::TextureUsage::None};
    rhi::LoadOp loadOp{rhi::LoadOp::Clear};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    float clearDepth{1.0F};
};

struct RenderTargetDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<RenderTargetColorAttachmentDesc> colorAttachments;
    std::optional<RenderTargetDepthAttachmentDesc> depthAttachment;
    std::string debugName;
};

// Owns the textures and views that form an offscreen render target. RenderGraph owns pass
// scheduling and state transitions; RenderTarget only supplies attachment metadata and keeps the
// persistent state of its images across graph instances.
class RenderTarget final {
public:
    explicit RenderTarget(rhi::IDevice& device);
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = delete;
    RenderTarget& operator=(RenderTarget&&) = delete;

    [[nodiscard]] bool create(RenderTargetDesc desc);
    [[nodiscard]] bool resize(std::uint32_t width, std::uint32_t height);
    void release();

    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::uint32_t width() const { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const { return desc_.height; }
    [[nodiscard]] std::size_t colorAttachmentCount() const { return colors_.size(); }
    [[nodiscard]] bool hasDepthAttachment() const { return depth_.has_value(); }
    [[nodiscard]] const RenderTargetDesc& desc() const { return desc_; }

    [[nodiscard]] rhi::TextureHandle colorTexture(std::size_t index) const;
    [[nodiscard]] rhi::TextureViewHandle colorView(std::size_t index) const;
    [[nodiscard]] rhi::TextureFormat colorFormat(std::size_t index) const;
    [[nodiscard]] rhi::TextureHandle depthTexture() const;
    [[nodiscard]] rhi::TextureViewHandle depthView() const;
    [[nodiscard]] rhi::TextureFormat depthFormat() const;

    [[nodiscard]] rhi::RenderingInfo renderingInfo() const;

    void import(RenderGraph& graph,
                rhi::ResourceState colorFinalState = rhi::ResourceState::ColorAttachment,
                rhi::ResourceState depthFinalState = rhi::ResourceState::DepthAttachment);
    [[nodiscard]] RgTextureHandle importColor(
        RenderGraph& graph,
        std::size_t index,
        rhi::ResourceState finalState = rhi::ResourceState::ColorAttachment);
    [[nodiscard]] RgTextureHandle importDepth(
        RenderGraph& graph,
        rhi::ResourceState finalState = rhi::ResourceState::DepthAttachment);

private:
    struct Attachment {
        rhi::TextureHandle texture;
        rhi::TextureViewHandle view;
        rhi::TextureFormat format{rhi::TextureFormat::Undefined};
        rhi::ResourceState state{rhi::ResourceState::Undefined};
    };

    [[nodiscard]] bool validate(const RenderTargetDesc& desc) const;
    [[nodiscard]] bool createAttachment(rhi::TextureFormat format,
                                        rhi::TextureAspect aspect,
                                        rhi::TextureUsage usage,
                                        std::string debugName,
                                        Attachment& destination);
    void releaseAttachment(Attachment& attachment);
    void releaseResources(std::vector<Attachment>& colors, std::optional<Attachment>& depth);
    [[nodiscard]] const Attachment& requireColor(std::size_t index) const;
    [[nodiscard]] Attachment& requireColor(std::size_t index);
    [[nodiscard]] const Attachment& requireDepth() const;
    [[nodiscard]] Attachment& requireDepth();

    rhi::IDevice& device_;
    RenderTargetDesc desc_;
    std::vector<Attachment> colors_;
    std::optional<Attachment> depth_;
};

} // namespace engine
