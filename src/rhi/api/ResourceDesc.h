#pragma once

#include "core/base/RID.h"
#include "core/math/Math.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Single home for every backend-agnostic RHI descriptor, enumeration and plain-data
// parameter struct. This header carries no backend-native (e.g. Vulkan) types, so the
// whole engine above the RHI can include it. The concrete RHI objects (buffer, texture,
// view, sampler, shader, pipeline, bind group, command buffer) are never exposed here:
// layer 2 sees them only as engine::rhi::RID handles, and Device.h declares the create /
// destroy / upload operations that mint and reclaim those handles.
namespace engine::rhi {

// Every RHI resource handle is the unified 64-bit engine::RID. This alias exposes the
// name as rhi::RID so namespace-qualified call sites keep compiling.
using RID = ::engine::RID;

// Discriminator for the kinds of GPU object the device owns. RIDs are not self-typing
// (a handle's kind is implied by the API that produced it), so this enum names the object
// families for the device's per-type handle pools, logging and assertions.
enum class ResourceType {
    Buffer,
    Texture,
    TextureView,
    Sampler,
    Shader,
    GraphicsPipeline,
    BindGroupLayout,
    BindGroup,
    CommandBuffer,
};

// ---------------------------------------------------------------------------
// Pixel formats and vertex input.
// ---------------------------------------------------------------------------

enum class PixelFormat {
    Undefined,
    Rgba8Unorm,
    Rgba8Srgb,
    Bgra8Unorm,
    Bgra8Srgb,
    Depth32Float,
};

[[nodiscard]] constexpr bool isColorFormat(PixelFormat format) {
    return format == PixelFormat::Rgba8Unorm || format == PixelFormat::Rgba8Srgb ||
           format == PixelFormat::Bgra8Unorm || format == PixelFormat::Bgra8Srgb;
}

[[nodiscard]] constexpr bool isDepthFormat(PixelFormat format) {
    return format == PixelFormat::Depth32Float;
}

enum class VertexFormat {
    Float32,
    Vec2Float32,
    Vec3Float32,
    Vec4Float32,
    UInt16x4,
    UInt8x4Normalized,
};

enum class VertexInputRate {
    Vertex,
    Instance,
};

struct VertexStreamDesc {
    std::uint32_t binding{};
    std::uint32_t location{};
    VertexFormat format{VertexFormat::Float32};
    std::uint32_t stride{};
    VertexInputRate inputRate{VertexInputRate::Vertex};
};

enum class IndexFormat { UInt16, UInt32 };

// ---------------------------------------------------------------------------
// Pipeline state enumerations.
// ---------------------------------------------------------------------------

enum class PrimitiveTopology {
    TriangleList,
    LineList,
};

enum class CullMode {
    None,
    Front,
    Back,
};

enum class FrontFace {
    Clockwise,
    CounterClockwise,
};

enum class FillMode {
    Solid,
    Wireframe,
};

enum class CompareOp {
    Never,
    Less,
    LessEqual,
    Equal,
    Greater,
    GreaterEqual,
    Always,
};

enum class BlendMode {
    Off,
    Alpha,
    Additive,
    PremultipliedAlpha,
};

enum class ColorWriteMask : std::uint8_t {
    None = 0,
    Red = 1U << 0U,
    Green = 1U << 1U,
    Blue = 1U << 2U,
    Alpha = 1U << 3U,
    All = 0x0FU,
};

constexpr ColorWriteMask operator|(ColorWriteMask left, ColorWriteMask right) {
    return static_cast<ColorWriteMask>(static_cast<std::uint8_t>(left) |
                                       static_cast<std::uint8_t>(right));
}

constexpr bool hasFlag(ColorWriteMask value, ColorWriteMask flag) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

// GraphicsPipelineDesc only contains static pipeline state. All dynamic states
// (cull mode, front face, depth test/write/compare, blend mode, color write mask,
// primitive topology and fill mode) are set at command buffer record time through
// the RHI command buffer and do not contribute to the pipeline layout or cache key.
struct GraphicsPipelineDesc {
    RID vertexShader;
    std::string vertexEntry{"main"};
    RID fragmentShader;
    std::string fragmentEntry{"main"};
    std::vector<RID> bindGroupLayouts;
    std::vector<VertexStreamDesc> vertexStreams;
    std::vector<PixelFormat> colorFormats;
    PixelFormat depthFormat{PixelFormat::Undefined};
};

// ---------------------------------------------------------------------------
// Buffers.
// ---------------------------------------------------------------------------

enum class BufferUsage : std::uint32_t {
    None = 0,
    Vertex = 1U << 0U,
    Index = 1U << 1U,
    Uniform = 1U << 2U,
    Storage = 1U << 3U,
    TransferSource = 1U << 4U,
    TransferDestination = 1U << 5U,
};

constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) |
                                    static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(BufferUsage value, BufferUsage flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class MemoryUsage {
    DeviceLocal,
    Upload,
    Readback,
};

struct BufferDesc {
    std::uint64_t size{};
    BufferUsage usage{BufferUsage::None};
    MemoryUsage memoryUsage{MemoryUsage::DeviceLocal};
    std::string debugName;
};

// ---------------------------------------------------------------------------
// Textures and texture views.
// ---------------------------------------------------------------------------

enum class TextureType { Texture2D, Texture2DArray, Texture3D, TextureCube, TextureCubeArray };

enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1U << 0U,
    TransferSource = 1U << 1U,
    TransferDestination = 1U << 2U,
    ColorAttachment = 1U << 3U,
    DepthStencilAttachment = 1U << 4U,
};

constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) |
                                     static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(TextureUsage value, TextureUsage flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

// Image tiling and multisample count (map to VkImageTiling / VkSampleCountFlagBits).
enum class TextureTiling { Optimal, Linear };
enum class SampleCount { One, Two, Four, Eight };

struct TextureDesc {
    TextureType dimension{TextureType::Texture2D};
    PixelFormat format{PixelFormat::Undefined};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t arrayLayers{1};
    std::uint32_t mipCount{1};
    TextureUsage usage{TextureUsage::None};
    TextureTiling tiling{TextureTiling::Optimal};
    SampleCount samples{SampleCount::One};
    std::string debugName;
};

struct TextureUploadRegion {
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::span<const std::byte> data;
};

enum class TextureAspect { Color, Depth };

enum class SwizzleComponent { Identity, Zero, One, R, G, B, A };

struct TextureSwizzle {
    SwizzleComponent r{SwizzleComponent::Identity};
    SwizzleComponent g{SwizzleComponent::Identity};
    SwizzleComponent b{SwizzleComponent::Identity};
    SwizzleComponent a{SwizzleComponent::Identity};

    [[nodiscard]] bool operator==(const TextureSwizzle&) const = default;
};

struct TextureViewDesc {
    TextureType type{TextureType::Texture2D};
    // Undefined means use the source texture's pixel format.
    PixelFormat format{PixelFormat::Undefined};
    std::uint32_t baseMip{};
    std::uint32_t mipCount{1};
    std::uint32_t baseLayer{};
    std::uint32_t layerCount{1};
    TextureAspect aspect{TextureAspect::Color};
    TextureSwizzle swizzle;

    [[nodiscard]] bool operator==(const TextureViewDesc&) const = default;
};

struct TextureViewDescHash {
    [[nodiscard]] std::size_t operator()(const TextureViewDesc& desc) const noexcept {
        std::size_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::size_t value) { hash = (hash ^ value) * 1099511628211ULL; };
        mix(static_cast<std::size_t>(desc.type));
        mix(static_cast<std::size_t>(desc.format));
        mix(desc.baseMip);
        mix(desc.mipCount);
        mix(desc.baseLayer);
        mix(desc.layerCount);
        mix(static_cast<std::size_t>(desc.aspect));
        mix(static_cast<std::size_t>(desc.swizzle.r));
        mix(static_cast<std::size_t>(desc.swizzle.g));
        mix(static_cast<std::size_t>(desc.swizzle.b));
        mix(static_cast<std::size_t>(desc.swizzle.a));
        return hash;
    }
};

// A resolved (view, sampler) pair bound to a shader sampling slot.
struct TextureBinding {
    RID view;
    RID sampler;

    [[nodiscard]] bool operator==(const TextureBinding&) const = default;
};

// ---------------------------------------------------------------------------
// Samplers.
// ---------------------------------------------------------------------------

enum class SamplerFilter {
    Nearest,
    Linear,
};

enum class SamplerMipmapFilter { Nearest, Linear };
enum class SamplerAddressMode { Repeat, MirroredRepeat, ClampToEdge };
enum class SamplerBorderColor { TransparentBlack, OpaqueBlack, OpaqueWhite };

// Sentinel matching VK_LOD_CLAMP_NONE.
inline constexpr float kLodClampNone = 1000.0F;

struct SamplerDesc {
    SamplerFilter minFilter{SamplerFilter::Linear};
    SamplerFilter magFilter{SamplerFilter::Linear};
    SamplerMipmapFilter mipmapFilter{SamplerMipmapFilter::Linear};
    SamplerAddressMode addressU{SamplerAddressMode::Repeat};
    SamplerAddressMode addressV{SamplerAddressMode::Repeat};
    SamplerBorderColor borderColor{SamplerBorderColor::OpaqueWhite};
    float maxAnisotropy{1.0F};
    float minLod{0.0F};
    float maxLod{kLodClampNone};
    bool compareEnable{false};
    CompareOp compareOp{CompareOp::LessEqual};

    [[nodiscard]] bool operator==(const SamplerDesc&) const = default;
};

// Descriptor-value hash so a SamplerDesc can key a device-side dedup cache: samplers are pure
// value objects, so identical descriptors resolve to one shared handle.
struct SamplerDescHash {
    [[nodiscard]] std::size_t operator()(const SamplerDesc& desc) const noexcept {
        std::size_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::size_t value) { hash = (hash ^ value) * 1099511628211ULL; };
        mix(static_cast<std::size_t>(desc.minFilter));
        mix(static_cast<std::size_t>(desc.magFilter));
        mix(static_cast<std::size_t>(desc.mipmapFilter));
        mix(static_cast<std::size_t>(desc.addressU));
        mix(static_cast<std::size_t>(desc.addressV));
        mix(static_cast<std::size_t>(desc.borderColor));
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.maxAnisotropy)));
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.minLod)));
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.maxLod)));
        mix(static_cast<std::size_t>(desc.compareEnable));
        mix(static_cast<std::size_t>(desc.compareOp));
        return hash;
    }
};

// ---------------------------------------------------------------------------
// Shaders.
// ---------------------------------------------------------------------------

enum class ShaderStage {
    Vertex,
    Fragment,
};

struct ShaderDesc {
    ShaderStage stage{ShaderStage::Vertex};
    std::span<const std::byte> bytecode;
    std::string debugName;
};

// ---------------------------------------------------------------------------
// Bind group layouts and bind groups.
// ---------------------------------------------------------------------------

enum class ShaderVisibility : std::uint32_t {
    None = 0,
    Vertex = 1U << 0U,
    Fragment = 1U << 1U,
};

constexpr ShaderVisibility operator|(ShaderVisibility left, ShaderVisibility right) {
    return static_cast<ShaderVisibility>(static_cast<std::uint32_t>(left) |
                                         static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(ShaderVisibility value, ShaderVisibility flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class BindingType {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
};

struct BindGroupLayoutEntry {
    std::uint32_t binding{};
    BindingType type{BindingType::UniformBuffer};
    ShaderVisibility visibility{ShaderVisibility::None};
};

struct BindGroupLayoutDesc {
    std::span<const BindGroupLayoutEntry> entries;
    std::string debugName;
};

struct BindGroupEntry {
    std::uint32_t binding{};
    BindingType type{BindingType::UniformBuffer};
    RID buffer;
    std::uint64_t offset{};
    std::uint64_t size{};
    RID textureView;
    RID sampler;
};

struct BindGroupDesc {
    RID layout;
    std::span<const BindGroupEntry> entries;
    std::string debugName;
};

// ---------------------------------------------------------------------------
// Swapchain.
// ---------------------------------------------------------------------------

enum class FrameStatus {
    Ready,
    OutOfDate,
};

struct SwapchainDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    bool vsync{true};
};

// ---------------------------------------------------------------------------
// Command recording parameter types.
// ---------------------------------------------------------------------------

enum class LoadOp { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };

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
    RID view;
    LoadOp loadOp{LoadOp::Load};
    StoreOp storeOp{StoreOp::Store};
    math::Vec4 clearColor{0.0F};
};

struct DepthAttachment {
    RID view;
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

struct BufferCopy {
    RID source;
    RID destination;
    std::uint64_t sourceOffset{};
    std::uint64_t destinationOffset{};
    std::uint64_t size{};
};

struct ImageCopy {
    RID source;
    RID destination;
    std::uint32_t sourceMipLevel{};
    std::uint32_t destinationMipLevel{};
    std::uint32_t sourceArrayLayer{};
    std::uint32_t destinationArrayLayer{};
    Offset3D sourceOffset;
    Offset3D destinationOffset;
    Extent3D extent;
};

struct BufferImageCopy {
    RID buffer;
    RID texture;
    std::uint64_t bufferOffset{};
    std::uint32_t bufferRowLength{};
    std::uint32_t bufferImageHeight{};
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    Offset3D textureOffset;
    Extent3D extent;
};

struct BufferUpdate {
    RID destination;
    std::uint64_t offset{};
    std::span<const std::byte> data;
};

struct ImageUpdate {
    RID destination;
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    Offset3D offset;
    Extent3D extent;
    std::span<const std::byte> data;
};

struct TextureBarrier {
    RID texture;
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
