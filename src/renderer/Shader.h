#pragma once

#include "asset/Asset.h"
#include "core/io/VirtualPath.h"
#include "math/Math.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace engine {

class ShaderAsset;

enum class ShaderPropertyType { Float, Range, Vec2, Vec3, Vec4, Color, Texture2D, Boolean };
enum class ShaderPassType { Forward, DepthOnly, ShadowCaster };
enum class CullMode { Off, Front, Back };
enum class FrontFace { Clockwise, CounterClockwise };
enum class FillMode { Solid, Wireframe };
enum class PrimitiveTopology { TriangleList, LineList };
enum class DepthCompare { Never, Less, LessEqual, Equal, Greater, GreaterEqual, Always };
enum class BlendMode { Off, Alpha, Additive, PremultipliedAlpha };
enum class ShaderValueType { Float, Vec2, Vec3, Vec4 };
enum class ShaderInterpolation { Smooth, Flat, NoPerspective };

struct ShaderVariantKey {
    std::uint64_t keywordBits{};
    std::uint32_t meshFeatureBits{};
    std::uint32_t platformFeatureBits{};

    bool operator==(const ShaderVariantKey&) const = default;
};

class ShaderKeywordSchema final {
public:
    ShaderKeywordSchema() = default;
    explicit ShaderKeywordSchema(std::vector<std::string> keywords);

    [[nodiscard]] const std::vector<std::string>& keywords() const {
        return keywords_;
    }
    [[nodiscard]] bool declares(std::string_view keyword) const;
    [[nodiscard]] ShaderVariantKey makeKey(
        std::span<const std::string> enabledKeywords,
        std::uint32_t meshFeatureBits = 0,
        std::uint32_t platformFeatureBits = 0) const;

private:
    std::vector<std::string> keywords_;
};

using ShaderValue =
    std::variant<float, bool, math::Vec2, math::Vec3, math::Vec4, std::string>;

struct ShaderPropertyDesc {
    std::string name;
    std::string displayName;
    ShaderPropertyType type{ShaderPropertyType::Float};
    ShaderValue defaultValue{0.0F};
    std::optional<math::Vec2> range;
    std::vector<std::string> attributes;
};

struct RenderStateDesc {
    CullMode cull{CullMode::Back};
    FrontFace frontFace{FrontFace::Clockwise};
    FillMode fill{FillMode::Solid};
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};
    bool depthWrite{true};
    DepthCompare depthTest{DepthCompare::LessEqual};
    BlendMode blend{BlendMode::Off};
    std::string colorMask{"RGBA"};
};

struct ShaderInterfaceVariable {
    std::string name;
    std::string semantic;
    ShaderValueType type{ShaderValueType::Float};
    std::uint32_t location{};
    ShaderInterpolation interpolation{ShaderInterpolation::Smooth};
};

struct ShaderProgramDesc {
    VirtualPath vertexSource;
    VirtualPath fragmentSource;

    [[nodiscard]] bool hasSourceProgram() const {
        return !vertexSource.empty() && !fragmentSource.empty();
    }
};

struct ShaderPassDesc {
    std::string name;
    ShaderPassType type{ShaderPassType::Forward};
    ShaderProgramDesc program;
    std::vector<ShaderInterfaceVariable> vertexInput;
    std::vector<ShaderInterfaceVariable> varyings;
    std::vector<ShaderInterfaceVariable> fragmentOutputs;
    std::vector<std::string> features;
};

struct ShaderPassAsset {
    ShaderPassDesc pass;
    RenderStateDesc renderState;
};

struct SubShaderDesc {
    std::string renderPipeline{"MiniForward"};
    std::string renderType{"Opaque"};
    int renderQueue{2000};
    std::vector<ShaderPassAsset> passes;

    [[nodiscard]] const ShaderPassDesc& requirePass(ShaderPassType type) const;
};

class ShaderAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Shader; }

    std::string name;
    std::vector<ShaderPropertyDesc> properties;
    std::vector<SubShaderDesc> subShaders;

    [[nodiscard]] const ShaderPropertyDesc* findProperty(const std::string& name) const;
}; 

class MaterialAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Material; }

    std::string name;
    VirtualPath shader;
    std::shared_ptr<ShaderAsset> shaderAsset;
    std::unordered_map<std::string, ShaderValue> properties;
    std::vector<std::string> keywords;
    std::optional<int> renderQueue;
};

namespace detail {

[[nodiscard]] std::shared_ptr<ShaderAsset> parseShaderAsset(
    const VirtualPath& path, std::string_view source);
[[nodiscard]] std::shared_ptr<MaterialAsset> parseMaterialAsset(
    const VirtualPath& path, std::string_view source);

} // namespace detail
[[nodiscard]] bool validateMaterialAsset(
    const MaterialAsset& material, const ShaderAsset& shader,
    const VirtualPath& materialPath);

struct UniformMemberLayout {
    std::string name;
    ShaderPropertyType type{ShaderPropertyType::Float};
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint32_t alignment{};
};

struct UniformBlockLayout {
    std::uint32_t byteSize{};
    std::vector<UniformMemberLayout> members;

    [[nodiscard]] const UniformMemberLayout* findMember(std::string_view name) const;
    [[nodiscard]] const UniformMemberLayout& requireMember(std::string_view name) const;
};

[[nodiscard]] bool isUniformProperty(ShaderPropertyType type);
[[nodiscard]] UniformBlockLayout
buildUniformBlockLayout(std::span<const ShaderPropertyDesc> properties);

enum class ShaderStage { Vertex, Fragment };
enum class ShaderDescriptorType {
    UniformBuffer,
    StorageBuffer,
    Sampler,
    SampledImage,
    CombinedImageSampler,
    Unknown,
};

struct ShaderStageVariable {
    std::string name;
    ShaderValueType type{ShaderValueType::Float};
    std::uint32_t location{};
};

struct ShaderUniformMember {
    std::string name;
    std::uint32_t offset{};
};

struct ShaderDescriptorBinding {
    std::string name;
    ShaderDescriptorType type{ShaderDescriptorType::Unknown};
    std::uint32_t set{};
    std::uint32_t binding{};
    std::vector<ShaderUniformMember> members;
};

struct SpirvReflection {
    ShaderStage stage{ShaderStage::Vertex};
    std::vector<ShaderStageVariable> inputs;
    std::vector<ShaderStageVariable> outputs;
    std::vector<ShaderDescriptorBinding> descriptors;
};

[[nodiscard]] std::shared_ptr<SpirvReflection> reflectSpirv(
    const VirtualPath& path);
[[nodiscard]] bool validateSpirvReflection(
    const ShaderAsset& shader, const ShaderPassDesc& pass,
    const VirtualPath& vertexSpirv,
    const VirtualPath& fragmentSpirv);

class ShaderPass final {
public:
    ShaderPass(const ShaderPassDesc& desc, const RenderStateDesc& renderState);

    [[nodiscard]] ShaderPassType type() const { return type_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const ShaderProgramDesc& program() const { return program_; }
    [[nodiscard]] const std::vector<ShaderInterfaceVariable>& vertexInput() const {
        return vertexInput_;
    }
    [[nodiscard]] const std::vector<ShaderInterfaceVariable>& varyings() const {
        return varyings_;
    }
    [[nodiscard]] const std::vector<ShaderInterfaceVariable>& fragmentOutputs() const {
        return fragmentOutputs_;
    }
    [[nodiscard]] const RenderStateDesc& renderState() const { return renderState_; }
    [[nodiscard]] const std::vector<std::string>& features() const { return features_; }
    [[nodiscard]] const ShaderKeywordSchema& keywordSchema() const {
        return keywordSchema_;
    }
    [[nodiscard]] ShaderVariantKey variantKey(
        std::span<const std::string> materialKeywords,
        std::uint32_t meshFeatureBits = 0,
        std::uint32_t platformFeatureBits = 0) const;

private:
    std::string name_;
    ShaderPassType type_{ShaderPassType::Forward};
    ShaderProgramDesc program_;
    std::vector<ShaderInterfaceVariable> vertexInput_;
    std::vector<ShaderInterfaceVariable> varyings_;
    std::vector<ShaderInterfaceVariable> fragmentOutputs_;
    RenderStateDesc renderState_;
    std::vector<std::string> features_;
    ShaderKeywordSchema keywordSchema_;
};

class SubShader final {
public:
    explicit SubShader(const SubShaderDesc& desc);

    [[nodiscard]] const std::string& renderPipeline() const { return renderPipeline_; }
    [[nodiscard]] const std::string& renderType() const { return renderType_; }
    [[nodiscard]] int renderQueue() const { return renderQueue_; }
    [[nodiscard]] const std::vector<ShaderPass>& passes() const { return passes_; }
    [[nodiscard]] const ShaderPass* findPass(ShaderPassType type) const;
    [[nodiscard]] const ShaderPass& requirePass(ShaderPassType type) const;
    [[nodiscard]] bool supports(std::string_view renderPipeline) const;

private:
    std::string renderPipeline_;
    std::string renderType_;
    int renderQueue_{2000};
    std::vector<ShaderPass> passes_;
};

class Shader final {
public:
    explicit Shader(const ShaderAsset& asset);

    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] const std::vector<ShaderPropertyDesc>& properties() const {
        return properties_;
    }
    [[nodiscard]] const UniformBlockLayout& uniformBlockLayout() const {
        return uniformBlockLayout_;
    }
    [[nodiscard]] const SubShader* selectSubShader(
        std::string_view renderPipeline) const;
    [[nodiscard]] const SubShader& requireSubShader(
        std::string_view renderPipeline) const;
    [[nodiscard]] const SubShader& defaultSubShader() const;
    [[nodiscard]] bool declaresKeyword(std::string_view keyword) const;
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    VirtualPath assetPath_;
    std::string name_;
    std::vector<ShaderPropertyDesc> properties_;
    UniformBlockLayout uniformBlockLayout_;
    std::vector<SubShader> subShaders_;
    std::uint64_t revision_{1};
};

} // namespace engine
