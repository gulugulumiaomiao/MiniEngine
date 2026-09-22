#pragma once

#include "core/base/Handle.h"
#include "core/math/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine::rhi {

struct BufferHandleTag;
struct TextureHandleTag;
struct TextureViewHandleTag;
struct GraphicsPipelineHandleTag;
struct BindGroupHandleTag;
struct ShaderHandleTag;
struct BindGroupLayoutHandleTag;
struct SamplerHandleTag;

using BufferHandle = Handle<BufferHandleTag>;
using TextureHandle = Handle<TextureHandleTag>;
using TextureViewHandle = Handle<TextureViewHandleTag>;
using GraphicsPipelineHandle = Handle<GraphicsPipelineHandleTag>;
using BindGroupHandle = Handle<BindGroupHandleTag>;
using ShaderHandle = Handle<ShaderHandleTag>;
using BindGroupLayoutHandle = Handle<BindGroupLayoutHandleTag>;
using SamplerHandle = Handle<SamplerHandleTag>;

enum class IndexFormat { UInt16, UInt32 };
enum class LoadOp { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };
enum class TextureAspect { Color, Depth };

enum class SwizzleComponent { Identity, Zero, One, R, G, B, A };

struct TextureSwizzle {
    SwizzleComponent r{SwizzleComponent::Identity};
    SwizzleComponent g{SwizzleComponent::Identity};
    SwizzleComponent b{SwizzleComponent::Identity};
    SwizzleComponent a{SwizzleComponent::Identity};

    [[nodiscard]] bool operator==(const TextureSwizzle&) const = default;
};

enum class ResourceState {
    Undefined,
    CopySource,
    CopyDestination,
    ShaderRead,
    ColorAttachment,
    DepthAttachment,
    Present,
};

struct Viewport {
    float x{};
    float y{};
    float width{};
    float height{};
    float minDepth{};
    float maxDepth{1.0F};
};

struct Rect {
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct ColorAttachment {
    TextureViewHandle view;
    LoadOp loadOp{LoadOp::Load};
    StoreOp storeOp{StoreOp::Store};
    math::Vec4 clearColor{0.0F};
};

struct DepthAttachment {
    TextureViewHandle view;
    LoadOp loadOp{LoadOp::Clear};
    StoreOp storeOp{StoreOp::Store};
    float clearDepth{1.0F};
};

struct RenderingInfo {
    Rect renderArea;
    std::vector<ColorAttachment> colorAttachments;
    std::vector<DepthAttachment> depthAttachments;
};

struct DrawArguments {
    std::uint32_t vertexCount{};
    std::uint32_t instanceCount{1};
    std::uint32_t firstVertex{};
    std::uint32_t firstInstance{};
};

struct DrawIndexedArguments {
    std::uint32_t indexCount{};
    std::uint32_t instanceCount{1};
    std::uint32_t firstIndex{};
    std::int32_t vertexOffset{};
    std::uint32_t firstInstance{};
};

struct BufferCopy {
    BufferHandle source;
    BufferHandle destination;
    std::uint64_t sourceOffset{};
    std::uint64_t destinationOffset{};
    std::uint64_t size{};
};

struct Offset3D {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t z{};
};

struct Extent3D {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
};

struct ImageCopy {
    TextureHandle source;
    TextureHandle destination;
    std::uint32_t sourceMipLevel{};
    std::uint32_t destinationMipLevel{};
    std::uint32_t sourceArrayLayer{};
    std::uint32_t destinationArrayLayer{};
    Offset3D sourceOffset;
    Offset3D destinationOffset;
    Extent3D extent;
};

struct BufferImageCopy {
    BufferHandle buffer;
    TextureHandle texture;
    std::uint64_t bufferOffset{};
    std::uint32_t bufferRowLength{};
    std::uint32_t bufferImageHeight{};
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    Offset3D textureOffset;
    Extent3D extent;
};

struct BufferUpdate {
    BufferHandle destination;
    std::uint64_t offset{};
    std::span<const std::byte> data;
};

struct ImageUpdate {
    TextureHandle destination;
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    Offset3D offset;
    Extent3D extent;
    std::span<const std::byte> data;
};

struct TextureBarrier {
    TextureHandle texture;
    TextureAspect aspect{TextureAspect::Color};
    ResourceState before{ResourceState::Undefined};
    ResourceState after{ResourceState::Undefined};
    // Subresource range affected by the transition. Transfer commands such as
    // copyBufferToImage operate on a specific mip level / array layer, so callers
    // must be able to barrier exactly the subresource they copy.
    std::uint32_t baseMipLevel{};
    std::uint32_t mipCount{1};
    std::uint32_t baseArrayLayer{};
    std::uint32_t layerCount{1};
};

// Range sentinels matching VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS.
inline constexpr std::uint32_t kRemainingMipLevels = 0xFFFFFFFFU;
inline constexpr std::uint32_t kRemainingArrayLayers = 0xFFFFFFFFU;

} // namespace engine::rhi
