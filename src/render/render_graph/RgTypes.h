#pragma once

#include "core/math/Math.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <functional>
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

// Description of a transient texture created by the RenderGraph; format determines its aspect.
struct RgTextureDesc {
    rhi::TextureType dimension{rhi::TextureType::Texture2D};
    rhi::PixelFormat format{rhi::PixelFormat::Undefined};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t arrayLayers{1};
    std::uint32_t mipCount{1};
    rhi::TextureUsage usage{rhi::TextureUsage::None};
    std::string debugName;
};

// Resource access declared by a pass. Compilation derives synchronization and pass
// dependencies from these declarations, and validates reads/writes and initialization.
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
    math::Vec4 clearColor{0.0F};
};

struct RgDepthAttachment {
    RgTextureHandle texture;
    rhi::LoadOp loadOp{rhi::LoadOp::Clear};
    rhi::StoreOp storeOp{rhi::StoreOp::Store};
    float clearDepth{1.0F};
    rhi::AccessMode access{rhi::AccessMode::ReadWrite};
};

struct RgRenderingInfo {
    rhi::Rect renderArea;
    std::vector<RgColorAttachment> colorAttachments;
    std::vector<RgDepthAttachment> depthAttachments;
    std::vector<RgTextureHandle> inputAttachments;
};

// Passed to grouped-pass callbacks after compilation. Use renderPass/subpass when
// creating or resolving a graphics pipeline; null renderPass means dynamic rendering.
struct RgExecuteContext {
    rhi::RID commandBuffer;
    rhi::RID renderPass;
    std::uint32_t subpass{};
};

struct RgRasterPass {
    std::string name;
    RgRenderingInfo rendering;
    std::vector<RgResourceAccess> resources;
    std::function<void(const RgExecuteContext&)> execute;
};

} // namespace engine
