#pragma once

#include "rhi/api/RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::rhi {

enum class TextureFormat {
    Undefined,
    Rgba8Unorm,
    Rgba8Srgb,
    Bgra8Unorm,
    Bgra8Srgb,
};

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

struct VertexBindingDesc {
    std::uint32_t binding{};
    std::uint32_t stride{};
    VertexInputRate inputRate{VertexInputRate::Vertex};
};

struct VertexAttributeDesc {
    std::uint32_t location{};
    std::uint32_t binding{};
    VertexFormat format{VertexFormat::Float32};
    std::uint32_t offset{};
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

struct RasterStateDesc {
    CullMode cull{CullMode::Back};
    FrontFace frontFace{FrontFace::Clockwise};
    FillMode fill{FillMode::Solid};
};

struct DepthStencilStateDesc {
    bool depthTestEnable{true};
    bool depthWriteEnable{true};
    CompareOp depthCompare{CompareOp::LessEqual};
};

struct BlendStateDesc {
    BlendMode mode{BlendMode::Off};
    ColorWriteMask colorWriteMask{ColorWriteMask::All};
};

struct GraphicsPipelineDesc {
    ShaderHandle vertexShader;
    std::string vertexEntry{"main"};
    ShaderHandle fragmentShader;
    std::string fragmentEntry{"main"};
    std::vector<BindGroupLayoutHandle> bindGroupLayouts;
    std::vector<VertexBindingDesc> vertexBindings;
    std::vector<VertexAttributeDesc> vertexAttributes;
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};
    RasterStateDesc raster;
    DepthStencilStateDesc depthStencil;
    BlendStateDesc blend;
    std::vector<TextureFormat> colorFormats;
};

} // namespace engine::rhi
