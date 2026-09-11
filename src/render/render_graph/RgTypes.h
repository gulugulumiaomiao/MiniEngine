#pragma once

#include "core/math/Math.h"
#include "rhi/api/ResourceDesc.h"
#include "rhi/api/RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

// Opaque handle to a texture node inside a RenderGraph.
struct RgTextureHandle {
    static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);

    std::uint32_t index{kInvalid};

    [[nodiscard]] bool valid() const { return index != kInvalid; }
    [[nodiscard]] bool operator==(RgTextureHandle other) const { return index == other.index; }
    [[nodiscard]] bool operator!=(RgTextureHandle other) const { return index != other.index; }
};

// Description of a transient texture created by the RenderGraph.
struct RgTextureDesc {
    rhi::TextureDimension dimension{rhi::TextureDimension::Texture2D};
    rhi::TextureFormat format{rhi::TextureFormat::Undefined};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t mipCount{1};
    rhi::TextureUsage usage{rhi::TextureUsage::None};
    rhi::TextureAspect aspect{rhi::TextureAspect::Color};
    std::string debugName;
};

// Resource usage declared by a pass. The RenderGraph uses this list to emit barriers.
struct RgResourceUsage {
    RgTextureHandle texture;
    rhi::TextureAspect aspect{rhi::TextureAspect::Color};
    rhi::ResourceState state{rhi::ResourceState::ShaderRead};
};

// Attachment description used during graph recording, before RHI handles are resolved.
struct RgColorAttachment {
    RgTextureHandle texture;
    rhi::LoadOp loadOp{rhi::LoadOp::Load};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    math::Vec4 clearColor{0.0F};
};

struct RgDepthAttachment {
    RgTextureHandle texture;
    rhi::LoadOp loadOp{rhi::LoadOp::Clear};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    float clearDepth{1.0F};
};

struct RgRenderingInfo {
    rhi::Rect renderArea;
    std::vector<RgColorAttachment> colorAttachments;
    std::vector<RgDepthAttachment> depthAttachments;
};

} // namespace engine
