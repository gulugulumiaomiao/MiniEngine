#pragma once

#include "rhi/api/RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::rhi {

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

} // namespace engine::rhi
