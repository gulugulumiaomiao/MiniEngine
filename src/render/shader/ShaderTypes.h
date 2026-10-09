#pragma once

#include "core/filesystem/VirtualPath.h"
#include "core/math/Math.h"
#include "core/math/hash.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace engine {

enum class ShaderPropertyType { Float, Vec2, Vec3, Vec4, Texture2D, Boolean, Matrix };
enum class ShaderPassType { Forward, DepthOnly, ShadowCaster };
enum class CullMode { Off, Front, Back };
enum class FrontFace { Clockwise, CounterClockwise };
enum class FillMode { Solid, Wireframe };
enum class PrimitiveTopology { TriangleList, LineList };
enum class DepthCompare { Never, Less, LessEqual, Equal, Greater, GreaterEqual, Always };
enum class BlendMode { Off, Alpha, Additive, PremultipliedAlpha };
enum class ShaderValueType { Float, Vec2, Vec3, Vec4 };
enum class ShaderInterpolation { Smooth, Flat, NoPerspective };

// GLSL uniform reflection types; independent of asset serialization and RHI.
enum class UboUniformType {
    Bool,
    Int,
    UInt,
    Float,
    BVec2,
    IVec2,
    UVec2,
    Vec2,
    BVec3,
    IVec3,
    UVec3,
    Vec3,
    BVec4,
    IVec4,
    UVec4,
    Vec4,
    Mat2,
    Mat3,
    Mat4,
};

using ShaderProgramId = Hash64;

class ShaderKeywordSchema final {
public:
    ShaderKeywordSchema() = default;
    explicit ShaderKeywordSchema(std::vector<std::string> keywords);

    [[nodiscard]] const std::vector<std::string>& keywords() const { return keywords_; }
    [[nodiscard]] bool declares(std::string_view keyword) const;
    [[nodiscard]] std::uint64_t makeKey(std::span<const std::string> enabledKeywords) const;

private:
    std::vector<std::string> keywords_;
};

using ShaderValue =
    std::variant<float, bool, math::Vec2, math::Vec3, math::Vec4, std::string, math::Mat44>;

struct ShaderPropertyInfo {
    std::string name;
    ShaderPropertyType type{ShaderPropertyType::Float};
    ShaderValue defaultValue{0.0F};
};

struct RenderState {
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

struct ShaderPassCreateInfo {
    std::string name;
    ShaderPassType type{ShaderPassType::Forward};
    ShaderProgramDesc program;
    std::vector<ShaderInterfaceVariable> vertexInput;
    std::vector<ShaderInterfaceVariable> varyings;
    std::vector<ShaderInterfaceVariable> fragmentOutputs;
    std::vector<std::string> features;
    RenderState renderState;
};

struct SubShaderCreateInfo {
    std::string renderPipeline{"MiniForward"};
    int renderQueue{2000};
    std::vector<ShaderPassCreateInfo> passes;
};

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
buildUniformBlockLayout(std::span<const ShaderPropertyInfo> properties);

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
    UboUniformType type{UboUniformType::Float};
    std::uint32_t offset{};
    std::uint32_t size{};
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

// Opaque, stable program locator supplied by the producer (including offline tools).
using ShaderCompileKey = Hash64;

struct ShaderCreateInfo {
    std::string name;
    std::vector<ShaderPropertyInfo> properties;
    std::vector<ShaderPropertyInfo> globalProperties;
    std::vector<SubShaderCreateInfo> subShaders;
    ShaderCompileKey compileKey{};
};

[[nodiscard]] bool validateShaderCreateInfo(const ShaderCreateInfo& info);

} // namespace engine
