#pragma once

#include "core/math/Math.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {

class RenderContext;
class RgFrameResources;

// Opaque handle to a texture node inside a RenderGraph.
struct RgTextureHandle {
    static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);

    std::uint32_t index{kInvalid};

    [[nodiscard]] bool valid() const { return index != kInvalid; }
    [[nodiscard]] bool operator==(RgTextureHandle other) const { return index == other.index; }
    [[nodiscard]] bool operator!=(RgTextureHandle other) const { return index != other.index; }
};

// Compiled resource access derived from a pass's attachments and sampled textures.
// Used for synchronization, pass dependencies and initialization validation.
struct RgResourceAccess {
    RgTextureHandle texture;
    rhi::TextureAspect aspect{rhi::TextureAspect::Color};
    rhi::ResourceState state{rhi::ResourceState::ShaderRead};
    rhi::AccessMode access{rhi::AccessMode::Auto};
};

// Attachment description used during graph recording, before RHI handles are resolved.
struct RgColorAttachment {
    RgTextureHandle texture;
    rhi::LoadOp loadOp{rhi::LoadOp::Load};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    rhi::ColorClearValue clearColor;
    std::function<rhi::ColorClearValue(const RenderContext&)> clearColorResolver;
};

struct RgDepthAttachment {
    RgTextureHandle texture;
    rhi::LoadOp loadOp{rhi::LoadOp::Clear};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    float clearDepth{1.0F};
    rhi::AccessMode access{rhi::AccessMode::ReadWrite};
    std::function<float(const RenderContext&)> clearDepthResolver;
};

// Passed to raster-pass callbacks after compilation. Use renderPass/subpass when
// creating or resolving a graphics pipeline; null renderPass means dynamic rendering.
struct RgExecuteContext {
    rhi::RID commandBuffer;
    rhi::RID renderPass;
    std::uint32_t subpass{};
    RenderContext& frame;
    const RgFrameResources& resources;
    rhi::Rect renderArea;
};

struct RgRasterPass {
    std::string name;
    rhi::Rect renderArea;
    std::function<rhi::Rect(const RenderContext&)> renderAreaResolver;
    std::vector<RgColorAttachment> colorAttachments;
    std::vector<RgDepthAttachment> depthAttachments;
    std::vector<RgTextureHandle> inputAttachments;
    std::vector<RgTextureHandle> sampledTextures;
    std::function<void(const RgExecuteContext&)> execute;
};

} // namespace engine
