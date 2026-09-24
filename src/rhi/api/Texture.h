#pragma once

#include "rhi/api/PipelineDesc.h" // PixelFormat
#include "rhi/api/RhiTypes.h"     // TextureTiling / SampleCount

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace engine::rhi {

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

// Thin, opaque handle target. The concrete backend texture (e.g. VulkanTexture) retains its own
// native creation info (VkImage / VkFormat / extent / usage ...) and exposes backend-specific
// accessors; the RHI interface carries no descriptors and no view management — views are
// deduped and owned at the device level (IDevice::createTextureView / defaultTextureView).
class IRHITexture {
public:
    virtual ~IRHITexture() = default;
};

} // namespace engine::rhi
