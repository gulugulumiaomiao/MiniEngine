#pragma once

#include "core/filesystem/VirtualPath.h"
#include "core/math/Math.h"
#include "core/serialization/Transferable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace engine {

// Persistent schema. No runtime shader types are used by these structures.
enum class ShaderAssetPropertyType {
    Float,
    Range,
    Vec2,
    Vec3,
    Vec4,
    Color,
    Texture2D,
    Boolean,
    Matrix
};
enum class ShaderAssetPassType { Forward, DepthOnly, ShadowCaster };
enum class ShaderAssetCullMode { Off, Front, Back };
enum class ShaderAssetFrontFace { Clockwise, CounterClockwise };
enum class ShaderAssetFillMode { Solid, Wireframe };
enum class ShaderAssetPrimitiveTopology { TriangleList, LineList };
enum class ShaderAssetDepthCompare { Never, Less, LessEqual, Equal, Greater, GreaterEqual, Always };
enum class ShaderAssetBlendMode { Off, Alpha, Additive, PremultipliedAlpha };
enum class ShaderAssetValueType { Float, Vec2, Vec3, Vec4, Uint };
enum class ShaderAssetInterpolation { Smooth, Flat, NoPerspective };

using ShaderAssetValue =
    std::variant<float, bool, math::Vec2, math::Vec3, math::Vec4, std::string, math::Mat44>;

struct ShaderPropertyAssetData : public Transferable {
    std::string name;
    std::string displayName;
    ShaderAssetPropertyType type{ShaderAssetPropertyType::Float};
    ShaderAssetValue defaultValue{0.0F};
    std::optional<math::Vec2> range;
    std::vector<std::string> attributes;

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct ShaderRenderStateAssetData : public Transferable {
    ShaderAssetCullMode cull{ShaderAssetCullMode::Back};
    ShaderAssetFrontFace frontFace{ShaderAssetFrontFace::Clockwise};
    ShaderAssetFillMode fill{ShaderAssetFillMode::Solid};
    ShaderAssetPrimitiveTopology topology{ShaderAssetPrimitiveTopology::TriangleList};
    bool depthWrite{true};
    ShaderAssetDepthCompare depthTest{ShaderAssetDepthCompare::LessEqual};
    ShaderAssetBlendMode blend{ShaderAssetBlendMode::Off};
    std::string colorMask{"RGBA"};

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct ShaderInterfaceAssetData : public Transferable {
    std::string name;
    std::string semantic;
    ShaderAssetValueType type{ShaderAssetValueType::Float};
    std::uint32_t location{};
    ShaderAssetInterpolation interpolation{ShaderAssetInterpolation::Smooth};

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct ShaderProgramAssetData {
    VirtualPath vertexSource;
    VirtualPath fragmentSource;

    [[nodiscard]] bool hasSourceProgram() const {
        return !vertexSource.empty() && !fragmentSource.empty();
    }
};

struct ShaderPassAssetDesc {
    std::string name;
    ShaderAssetPassType type{ShaderAssetPassType::Forward};
    ShaderProgramAssetData program;
    std::vector<ShaderInterfaceAssetData> vertexInput;
    std::vector<ShaderInterfaceAssetData> varyings;
    std::vector<ShaderInterfaceAssetData> fragmentOutputs;
    std::vector<std::string> features;
};

struct ShaderPassAssetData : public Transferable {
    ShaderPassAssetDesc pass;
    ShaderRenderStateAssetData renderState;

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct SubShaderAssetData : public Transferable {
    std::string renderPipeline{"MiniForward"};
    int renderQueue{2000};
    std::vector<ShaderPassAssetData> passes;

    [[nodiscard]] const ShaderPassAssetDesc& requirePass(ShaderAssetPassType type) const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct ShaderAssetData final : public Transferable {
    std::string name;
    std::vector<ShaderPropertyAssetData> properties;
    std::vector<ShaderPropertyAssetData> globalProperties;
    std::vector<SubShaderAssetData> subShaders;

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

} // namespace engine
