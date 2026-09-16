#include "render/shader/Shader.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

namespace engine {

namespace {

constexpr std::uint32_t kShaderAssetMagic = 0x52444853U;
constexpr std::uint16_t kShaderAssetVersion = 2;

template <typename Enum>
bool transferShaderEnum(Transfer& archive, std::string_view name, Enum& value, Enum maximum) {
    return archive.transfer(name, value) && value >= static_cast<Enum>(0) && value <= maximum;
}

} // namespace

bool ShaderInterfaceVariable::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", name) &&
           archive.transfer("semantic", semantic) &&
           transferShaderEnum(archive, "type", type, ShaderValueType::Vec4) &&
           archive.transfer("location", location) &&
           transferShaderEnum(
               archive, "interpolation", interpolation, ShaderInterpolation::NoPerspective) &&
           archive.endObject();
}

bool RenderStateDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && transferShaderEnum(archive, "cull", cull, CullMode::Back) &&
           transferShaderEnum(archive, "front_face", frontFace, FrontFace::CounterClockwise) &&
           transferShaderEnum(archive, "fill", fill, FillMode::Wireframe) &&
           transferShaderEnum(archive, "topology", topology, PrimitiveTopology::LineList) &&
           archive.transfer("depth_write", depthWrite) &&
           transferShaderEnum(archive, "depth_test", depthTest, DepthCompare::Always) &&
           transferShaderEnum(archive, "blend", blend, BlendMode::PremultipliedAlpha) &&
           archive.transfer("color_mask", colorMask) && archive.endObject();
}

bool ShaderPropertyDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", name) &&
           archive.transfer("display_name", displayName) &&
           transferShaderEnum(archive, "property_type", type, ShaderPropertyType::Boolean) &&
           archive.transfer("default_value", defaultValue) && archive.transfer("range", range) &&
           archive.transfer("attributes", attributes) && archive.endObject();
}

bool ShaderPassAsset::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", pass.name) &&
           transferShaderEnum(archive, "pass_type", pass.type, ShaderPassType::ShadowCaster) &&
           archive.transfer("vertex_source", pass.program.vertexSource) &&
           archive.transfer("fragment_source", pass.program.fragmentSource) &&
           archive.transfer("vertex_input", pass.vertexInput) &&
           archive.transfer("varyings", pass.varyings) &&
           archive.transfer("fragment_outputs", pass.fragmentOutputs) &&
           archive.transfer("features", pass.features) &&
           archive.transfer("render_state", renderState) && archive.endObject();
}

bool SubShaderDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("render_pipeline", renderPipeline) &&
           archive.transfer("render_queue", renderQueue) && archive.transfer("passes", passes) &&
           archive.endObject();
}

bool ShaderAsset::transfer(Transfer& archive) {
    ShaderAsset decoded;
    ShaderAsset& target = archive.reading() ? decoded : *this;
    std::uint32_t magic = kShaderAssetMagic;
    std::uint16_t version = kShaderAssetVersion;
    const bool succeeded = archive.beginObject({}) && archive.transfer("magic", magic) &&
                           magic == kShaderAssetMagic && archive.transfer("version", version) &&
                           version == kShaderAssetVersion &&
                           archive.transfer("name", target.name) &&
                           archive.transfer("properties", target.properties) &&
                           archive.transfer("sub_shaders", target.subShaders) &&
                           !target.subShaders.empty() && archive.endObject();
    if (!succeeded) {
        Log::error("ShaderAsset",
                   "Invalid payload %s: %s",
                   assetPath().string().c_str(),
                   archive.error().empty() ? "validation failed" : archive.error().c_str());
        return false;
    }
    if (archive.reading()) {
        name = std::move(decoded.name);
        properties = std::move(decoded.properties);
        subShaders = std::move(decoded.subShaders);
    }
    return true;
}

ShaderKeywordSchema::ShaderKeywordSchema(std::vector<std::string> keywords)
    : keywords_(std::move(keywords)) {
    std::ranges::sort(keywords_);
    keywords_.erase(std::unique(keywords_.begin(), keywords_.end()), keywords_.end());
    if (keywords_.size() > 64) {
        Log::fatal("ShaderKeywordSchema", "A pass cannot declare more than 64 keywords");
    }
}

bool ShaderKeywordSchema::declares(std::string_view keyword) const {
    return std::ranges::binary_search(keywords_, keyword);
}

ShaderVariantKey ShaderKeywordSchema::makeKey(std::span<const std::string> enabledKeywords,
                                              std::uint32_t meshFeatureBits,
                                              std::uint32_t platformFeatureBits) const {
    ShaderVariantKey result{0, meshFeatureBits, platformFeatureBits};
    for (const std::string& keyword : enabledKeywords) {
        // Material keywords may target any pass of the shader (e.g. RECEIVE_SHADOWS only
        // affects the Forward pass); passes that do not declare a keyword simply ignore it.
        const auto found = std::ranges::lower_bound(keywords_, keyword);
        if (found == keywords_.end() || *found != keyword) {
            continue;
        }
        const std::size_t bit = static_cast<std::size_t>(std::distance(keywords_.begin(), found));
        result.keywordBits |= std::uint64_t{1} << bit;
    }
    return result;
}

const ShaderPassDesc& SubShaderDesc::requirePass(ShaderPassType type) const {
    const auto found = std::ranges::find_if(
        passes, [type](const ShaderPassAsset& asset) { return asset.pass.type == type; });
    if (found == passes.end()) {
        Log::fatal("ShaderAsset", "SubShader has no required pass");
    }
    return found->pass;
}

const ShaderPropertyDesc* ShaderAsset::findProperty(const std::string& name) const {
    const auto found = std::ranges::find_if(
        properties, [&name](const ShaderPropertyDesc& property) { return property.name == name; });
    return found == properties.end() ? nullptr : &*found;
}

} // namespace engine

namespace engine {
namespace {

struct Std140TypeLayout {
    std::uint32_t size;
    std::uint32_t alignment;
};

Std140TypeLayout std140TypeLayout(ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range:
    case ShaderPropertyType::Boolean: return {4, 4};
    case ShaderPropertyType::Vec2: return {8, 8};
    case ShaderPropertyType::Vec3: return {16, 16};
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color: return {16, 16};
    case ShaderPropertyType::Texture2D:
        assert(false && "Texture2D is a descriptor, not a uniform member");
    }
    Log::fatal("ShaderLayout", "Unsupported Shader property type");
}

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment) {
    const std::uint64_t aligned =
        (static_cast<std::uint64_t>(value) + alignment - 1) & ~(alignment - 1ULL);
    if (aligned > std::numeric_limits<std::uint32_t>::max()) {
        Log::fatal("ShaderLayout", "Material uniform layout exceeds 32-bit size");
    }
    return static_cast<std::uint32_t>(aligned);
}

} // namespace

const UniformMemberLayout* UniformBlockLayout::findMember(std::string_view name) const {
    const auto member = std::ranges::find_if(
        members, [name](const UniformMemberLayout& item) { return item.name == name; });
    return member == members.end() ? nullptr : &*member;
}

const UniformMemberLayout& UniformBlockLayout::requireMember(std::string_view name) const {
    if (const UniformMemberLayout* member = findMember(name)) {
        return *member;
    }
    Log::fatal("ShaderLayout", "Material uniform member does not exist: " + std::string{name});
}

bool isUniformProperty(ShaderPropertyType type) {
    return type != ShaderPropertyType::Texture2D;
}

UniformBlockLayout buildUniformBlockLayout(std::span<const ShaderPropertyDesc> properties) {
    UniformBlockLayout result;
    std::uint32_t cursor = 0;
    for (const ShaderPropertyDesc& property : properties) {
        if (!isUniformProperty(property.type)) {
            continue;
        }
        const Std140TypeLayout typeLayout = std140TypeLayout(property.type);
        const std::uint32_t offset = alignUp(cursor, typeLayout.alignment);
        if (offset > std::numeric_limits<std::uint32_t>::max() - typeLayout.size) {
            Log::fatal("ShaderLayout", "Material uniform layout exceeds 32-bit size");
        }
        result.members.push_back(
            {property.name, property.type, offset, typeLayout.size, typeLayout.alignment});
        cursor = offset + typeLayout.size;
    }
    result.byteSize = result.members.empty() ? 0 : alignUp(cursor, 16);
    return result;
}

ShaderPass::ShaderPass(const ShaderPassDesc& desc, const RenderStateDesc& renderState)
    : name_(desc.name), type_(desc.type), program_(desc.program), vertexInput_(desc.vertexInput),
      varyings_(desc.varyings), fragmentOutputs_(desc.fragmentOutputs), renderState_(renderState),
      features_(desc.features), keywordSchema_(desc.features) {}

ShaderVariantKey ShaderPass::variantKey(std::span<const std::string> materialKeywords,
                                        std::uint32_t meshFeatureBits,
                                        std::uint32_t platformFeatureBits) const {
    return keywordSchema_.makeKey(materialKeywords, meshFeatureBits, platformFeatureBits);
}

SubShader::SubShader(const SubShaderDesc& desc)
    : renderPipeline_(desc.renderPipeline), renderQueue_(desc.renderQueue) {
    passes_.reserve(desc.passes.size());
    for (const ShaderPassAsset& asset : desc.passes) {
        passes_.emplace_back(asset.pass, asset.renderState);
    }
}

const ShaderPass* SubShader::findPass(ShaderPassType type) const {
    const auto found = std::ranges::find_if(
        passes_, [type](const ShaderPass& pass) { return pass.type() == type; });
    return found == passes_.end() ? nullptr : &*found;
}

const ShaderPass& SubShader::requirePass(ShaderPassType type) const {
    if (const ShaderPass* pass = findPass(type)) {
        return *pass;
    }
    Log::fatal("SubShader", "Required pass does not exist");
}

bool SubShader::supports(std::string_view renderPipeline) const {
    return renderPipeline_ == renderPipeline;
}

Shader::Shader(const ShaderAsset& asset)
    : assetPath_(asset.assetPath()), name_(asset.name), properties_(asset.properties),
      uniformBlockLayout_(buildUniformBlockLayout(properties_)) {
    subShaders_.reserve(asset.subShaders.size());
    for (const SubShaderDesc& subShader : asset.subShaders) {
        subShaders_.emplace_back(subShader);
    }
}

Shader Shader::clone() const {
    Shader copy = *this;
    copy.assetId_ = AssetId{};
    return copy;
}

void Shader::rebuildFromAsset(const ShaderAsset& asset) {
    if (revision_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::fatal("Shader", "Shader revision overflow");
    }
    const std::uint64_t nextRevision = revision_ + 1;
    *this = asset.instantiate();
    revision_ = nextRevision;
}

Shader ShaderAsset::instantiate() const {
    return Shader{*this};
}

const SubShader* Shader::selectSubShader(std::string_view renderPipeline) const {
    const auto found =
        std::ranges::find_if(subShaders_, [renderPipeline](const SubShader& subShader) {
            return subShader.supports(renderPipeline);
        });
    return found == subShaders_.end() ? nullptr : &*found;
}

const SubShader& Shader::requireSubShader(std::string_view renderPipeline) const {
    if (const SubShader* subShader = selectSubShader(renderPipeline)) {
        return *subShader;
    }
    Log::fatal("Shader",
               "No compatible SubShader for render pipeline: %.*s",
               static_cast<int>(renderPipeline.size()),
               renderPipeline.data());
}

const SubShader& Shader::defaultSubShader() const {
    if (subShaders_.empty()) {
        Log::fatal("Shader", "Shader has no SubShader: %s", name_.c_str());
    }
    return subShaders_.front();
}

bool Shader::declaresKeyword(std::string_view keyword) const {
    return std::ranges::any_of(subShaders_, [keyword](const SubShader& subShader) {
        return std::ranges::any_of(subShader.passes(), [keyword](const ShaderPass& pass) {
            return std::ranges::find(pass.features(), keyword) != pass.features().end();
        });
    });
}

} // namespace engine
