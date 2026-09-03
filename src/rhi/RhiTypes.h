#pragma once

#include "math/Math.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace engine::rhi {

inline constexpr std::uint32_t kInvalidHandleIndex =
    std::numeric_limits<std::uint32_t>::max();

template <typename Tag>
struct Handle {
    std::uint32_t index{kInvalidHandleIndex};
    std::uint32_t generation{};

    [[nodiscard]] explicit operator bool() const { return index != kInvalidHandleIndex; }
    bool operator==(const Handle&) const = default;
};

struct BufferTag;
struct TextureTag;
struct TextureViewTag;
struct GraphicsPipelineTag;
struct BindGroupTag;
struct ShaderTag;

using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using TextureViewHandle = Handle<TextureViewTag>;
using GraphicsPipelineHandle = Handle<GraphicsPipelineTag>;
using BindGroupHandle = Handle<BindGroupTag>;
using ShaderHandle = Handle<ShaderTag>;

enum class IndexFormat { UInt16, UInt32 };
enum class LoadOp { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };
enum class TextureAspect { Color, Depth };
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

struct TextureBarrier {
    TextureHandle texture;
    TextureAspect aspect{TextureAspect::Color};
    ResourceState before{ResourceState::Undefined};
    ResourceState after{ResourceState::Undefined};
};

} // namespace engine::rhi
