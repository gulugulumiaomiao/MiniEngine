#pragma once

#include "rhi/api/PipelineDesc.h"
#include "rhi/api/RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace engine::rhi {

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

enum class TextureDimension { Texture2D };

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

struct TextureDesc {
    TextureDimension dimension{TextureDimension::Texture2D};
    TextureFormat format{TextureFormat::Undefined};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t mipCount{1};
    TextureUsage usage{TextureUsage::None};
    std::string debugName;
};

struct TextureUploadRegion {
    std::uint32_t mipLevel{};
    std::uint32_t arrayLayer{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::span<const std::byte> data;
};

struct TextureViewDesc {
    TextureHandle texture;
    TextureFormat format{TextureFormat::Undefined};
    TextureAspect aspect{TextureAspect::Color};
    std::uint32_t baseMipLevel{};
    std::uint32_t mipCount{1};
};

struct BufferDesc {
    std::uint64_t size{};
    BufferUsage usage{BufferUsage::None};
    MemoryUsage memoryUsage{MemoryUsage::DeviceLocal};
    std::string debugName;
};

enum class ShaderStage {
    Vertex,
    Fragment,
};

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
    BufferHandle buffer;
    std::uint64_t offset{};
    std::uint64_t size{};
    TextureViewHandle textureView;
    SamplerHandle sampler;
};

struct TextureBinding {
    TextureViewHandle view;
    SamplerHandle sampler;
};

struct BindGroupDesc {
    BindGroupLayoutHandle layout;
    std::span<const BindGroupEntry> entries;
    std::string debugName;
};

struct ShaderDesc {
    ShaderStage stage{ShaderStage::Vertex};
    std::span<const std::byte> bytecode;
    std::string debugName;
};

} // namespace engine::rhi
