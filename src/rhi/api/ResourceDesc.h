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

// Descriptor resource types, mirroring the VK_DESCRIPTOR_TYPE family. A binding's type in a
// descriptor set layout / uniform set is one of these; the Vulkan backend maps it 1:1 onto the
// corresponding VkDescriptorType. It lives in the backend-agnostic api header so both the engine
// (uniform layer) and the RHI can name binding types with no Vulkan dependency.
enum class ResourceType {
    UniformBuffer,          // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    StorageBuffer,          // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    SampledImage,           // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
    CombinedImageSampler,   // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    Sampler,                // VK_DESCRIPTOR_TYPE_SAMPLER
    StorageImage,           // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    InputAttachment,        // VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
};

// ---------------------------------------------------------------------------
// Pixel formats and vertex input.
// ---------------------------------------------------------------------------

// Preserve existing values when adding formats: append or assign explicit values.
enum class PixelFormat {
    Undefined = 0,
    Rgba8Unorm = 1,
    Rgba8Srgb = 2,
    Bgra8Unorm = 3,
    Bgra8Srgb = 4,
    Depth32Float = 5,
    R8Unorm = 6,
    Rg8Unorm = 7,
    R16Float = 8,
    Rg16Float = 9,
    Rgba16Float = 10,
    R32Float = 11,
    Rg32Float = 12,
    Rgba32Float = 13,
    Depth16Unorm = 14,
};

enum class FormatNumericType { Undefined, Unorm, Snorm, Uint, Sint, Float };

// Static storage properties only. Device support for filtering, blending, usages,
// tiling and sample counts must be queried separately.
struct PixelFormatInfo {
    std::uint32_t blockWidth{};
    std::uint32_t blockHeight{};
    std::uint32_t bytesPerBlock{};
    std::uint32_t channelCount{};
    FormatNumericType numericType{FormatNumericType::Undefined};
    bool srgb{};
    bool depth{};
    bool stencil{};

    [[nodiscard]] constexpr bool valid() const {
        return blockWidth && blockHeight && bytesPerBlock && channelCount &&
               numericType != FormatNumericType::Undefined;
    }
};

[[nodiscard]] constexpr PixelFormatInfo formatInfo(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8Unorm:
    case PixelFormat::Bgra8Unorm:
        return {1, 1, 4, 4, FormatNumericType::Unorm, false, false, false};
    case PixelFormat::Rgba8Srgb:
    case PixelFormat::Bgra8Srgb:
        return {1, 1, 4, 4, FormatNumericType::Unorm, true, false, false};
    case PixelFormat::Depth32Float:
        return {1, 1, 4, 1, FormatNumericType::Float, false, true, false};
    case PixelFormat::R8Unorm:
        return {1, 1, 1, 1, FormatNumericType::Unorm, false, false, false};
    case PixelFormat::Rg8Unorm:
        return {1, 1, 2, 2, FormatNumericType::Unorm, false, false, false};
    case PixelFormat::R16Float:
        return {1, 1, 2, 1, FormatNumericType::Float, false, false, false};
    case PixelFormat::Rg16Float:
        return {1, 1, 4, 2, FormatNumericType::Float, false, false, false};
    case PixelFormat::Rgba16Float:
        return {1, 1, 8, 4, FormatNumericType::Float, false, false, false};
    case PixelFormat::R32Float:
        return {1, 1, 4, 1, FormatNumericType::Float, false, false, false};
    case PixelFormat::Rg32Float:
        return {1, 1, 8, 2, FormatNumericType::Float, false, false, false};
    case PixelFormat::Rgba32Float:
        return {1, 1, 16, 4, FormatNumericType::Float, false, false, false};
    case PixelFormat::Depth16Unorm:
        return {1, 1, 2, 1, FormatNumericType::Unorm, false, true, false};
    case PixelFormat::Undefined:
        break;
    }
    return {}; // Undefined and unrecognized values have no usable storage layout.
}

[[nodiscard]] constexpr bool isColorFormat(PixelFormat format) {
    const auto info = formatInfo(format);
    return info.valid() && !info.depth && !info.stencil;
}

[[nodiscard]] constexpr bool isDepthFormat(PixelFormat format) {
    return formatInfo(format).depth;
}

[[nodiscard]] constexpr bool isSrgbFormat(PixelFormat format) {
    return formatInfo(format).srgb;
}

[[nodiscard]] constexpr bool hasStencil(PixelFormat format) {
    return formatInfo(format).stencil;
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
enum class SampleCount { One, Two, Four, Eight };

struct GraphicsPipelineDesc {
    RID vertexShader;
    std::string vertexEntry{"main"};
    RID fragmentShader;
    std::string fragmentEntry{"main"};
    // A single pipeline-layout RID (minted by pipeline_layout_create) that bundles the per-set
    // descriptor set layouts this pipeline binds against.
    RID pipelineLayout;
    std::vector<VertexStreamDesc> vertexStreams;
    std::vector<PixelFormat> colorFormats;
    PixelFormat depthFormat{PixelFormat::Undefined};
    // Null selects dynamic rendering. Otherwise this identifies the cached render pass
    // and the subpass for which the pipeline is compiled.
    RID renderPass;
    std::uint32_t subpass{};
    SampleCount samples{SampleCount::One};
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
    InputAttachment = 1U << 5U,
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
// Descriptor set layouts, pipeline layouts and descriptor sets.
//
// The reflection-driven uniform-binding system (engine layer UniformSetLayout / UniformSet /
// PipelineLayout) is expressed through these backend-agnostic descriptors.
// ---------------------------------------------------------------------------

// One binding slot of a descriptor set layout: its index, reflected name and resource type.
struct ResourceBindingDesc {
    std::uint32_t binding{};
    std::string name;
    ResourceType type{ResourceType::UniformBuffer};
};

struct DescriptorSetLayoutDesc {
    std::span<const ResourceBindingDesc> bindings;   // ascending by binding index
    std::string debugName;
};

struct PipelineLayoutDesc {
    std::span<const RID> setLayouts;                 // descriptor_set_layout RIDs, set 0..N
    std::string debugName;
};

// A single descriptor write (the backend-agnostic form of one VkWriteDescriptorSet). Which of
// buffer / textureView / sampler is read depends on `type`.
struct DescriptorWrite {
    std::uint32_t binding{};
    ResourceType type{ResourceType::UniformBuffer};
    RID buffer;
    std::uint64_t offset{};
    std::uint64_t size{};
    RID textureView;
    RID sampler;
};

// Descriptor-set allocation request. `unorderedHash` (computed by the engine-layer
// UniformSetLayout, order-independent, keyed only on per-type descriptor counts) selects the
// device-side descriptor pool bucket; `bindings` lets the device derive the per-type counts it
// must reserve.
struct DescriptorSetAllocateInfo {
    RID layout{};
    std::uint64_t unorderedHash{};
    std::span<const ResourceBindingDesc> bindings;
    std::uint32_t setCount{1};
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
    InputAttachment,
    Present,
};

enum class AccessMode { Auto, Read, Write, ReadWrite };

inline constexpr std::uint32_t kUnusedAttachment = 0xFFFFFFFFU;

struct RenderPassAttachmentDesc {
    PixelFormat format{PixelFormat::Undefined};
    SampleCount samples{SampleCount::One};
    LoadOp loadOp{LoadOp::Load};
    StoreOp storeOp{StoreOp::Store};
    ResourceState initialState{ResourceState::Undefined};
    ResourceState finalState{ResourceState::ShaderRead};
    bool operator==(const RenderPassAttachmentDesc&) const = default;
};

struct AttachmentReference {
    std::uint32_t attachment{kUnusedAttachment};
    ResourceState state{ResourceState::ColorAttachment};
    bool operator==(const AttachmentReference&) const = default;
};

struct SubpassDesc {
    std::vector<AttachmentReference> colors;
    std::vector<AttachmentReference> inputs; // order matches shader input_attachment_index
    AttachmentReference depth{kUnusedAttachment, ResourceState::DepthAttachment};
    std::vector<std::uint32_t> preserves;
    bool operator==(const SubpassDesc&) const = default;
};

struct SubpassDependency {
    std::uint32_t source{};
    std::uint32_t destination{};
    ResourceState before{ResourceState::ColorAttachment};
    ResourceState after{ResourceState::InputAttachment};
    AccessMode sourceAccess{AccessMode::Auto};
    AccessMode destinationAccess{AccessMode::Auto};
    bool byRegion{true};
    // A final read-only use still performs a color/depth store operation.
    ResourceState destinationStoreState{ResourceState::Undefined};
    bool operator==(const SubpassDependency&) const = default;
};

struct RenderPassDesc {
    std::vector<RenderPassAttachmentDesc> attachments;
    std::vector<SubpassDesc> subpasses;
    std::vector<SubpassDependency> dependencies;
    bool operator==(const RenderPassDesc&) const = default;
};

struct FramebufferDesc {
    RID renderPass;
    std::vector<RID> views; // same order as RenderPassDesc::attachments
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t layers{1};
    bool operator==(const FramebufferDesc&) const = default;
};

struct TextureViewInfo {
    RID texture;
    PixelFormat format{PixelFormat::Undefined};
    SampleCount samples{SampleCount::One};
    TextureUsage usage{TextureUsage::None};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t baseMip{};
    std::uint32_t mipCount{1};
    std::uint32_t baseLayer{};
    std::uint32_t layerCount{1};
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

struct AttachmentClearValue {
    math::Vec4 color{0.0F};
    float depth{1.0F};
};

struct RenderPassBeginInfo {
    RID renderPass;
    RID framebuffer;
    Rect renderArea;
    std::vector<AttachmentClearValue> clearValues;
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
    AccessMode sourceAccess{AccessMode::Auto};
    AccessMode destinationAccess{AccessMode::Auto};
};

// Range sentinels matching VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS.
inline constexpr std::uint32_t kRemainingMipLevels = 0xFFFFFFFFU;
inline constexpr std::uint32_t kRemainingArrayLayers = 0xFFFFFFFFU;

} // namespace engine::rhi
