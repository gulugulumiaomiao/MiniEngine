#include "render/shader/Shader.h"

#include "core/logging/Log.h"
#include "render/global_uniform/GlobalUniformManager.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <limits>
#include <set>
#include <utility>
#include <unordered_set>

namespace engine {
namespace {

// Non-owning registry: Shader instances never move and unregister on destruction.
std::unordered_set<Shader*> liveShaders;

template <typename Enum> bool validEnum(Enum value, Enum maximum) {
    return value >= static_cast<Enum>(0) && value <= maximum;
}

bool validIdentifier(std::string_view name) {
    const auto letter = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    return !name.empty() && letter(name.front()) &&
           std::ranges::all_of(name, [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

bool validProperty(const ShaderPropertyInfo& property) {
    if (!validIdentifier(property.name))
        return false;
    switch (property.type) {
    case ShaderPropertyType::Float: return std::holds_alternative<float>(property.defaultValue);
    case ShaderPropertyType::Vec2: return std::holds_alternative<math::Vec2>(property.defaultValue);
    case ShaderPropertyType::Vec3: return std::holds_alternative<math::Vec3>(property.defaultValue);
    case ShaderPropertyType::Vec4: return std::holds_alternative<math::Vec4>(property.defaultValue);
    case ShaderPropertyType::Boolean: return std::holds_alternative<bool>(property.defaultValue);
    case ShaderPropertyType::Texture2D:
        return std::holds_alternative<std::string>(property.defaultValue);
    case ShaderPropertyType::Matrix:
        return std::holds_alternative<math::Mat44>(property.defaultValue);
    }
    return false;
}

bool validInterface(std::span<const ShaderInterfaceVariable> variables, bool allowUint = false) {
    std::set<std::string> names;
    std::set<std::uint32_t> locations;
    for (const auto& item : variables) {
        if (!validIdentifier(item.name) || !names.insert(item.name).second ||
            !locations.insert(item.location).second ||
            !validEnum(item.type, allowUint ? ShaderValueType::Uint : ShaderValueType::Vec4) ||
            !validEnum(item.interpolation, ShaderInterpolation::NoPerspective))
            return false;
    }
    return true;
}

struct Std140TypeLayout {
    std::uint32_t size;
    std::uint32_t alignment;
};

Std140TypeLayout std140TypeLayout(ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Boolean: return {4, 4};
    case ShaderPropertyType::Vec2: return {8, 8};
    case ShaderPropertyType::Vec3: return {16, 16};
    case ShaderPropertyType::Vec4: return {16, 16};
    case ShaderPropertyType::Matrix: return {64, 16};
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

bool validateShaderCreateInfo(const ShaderCreateInfo& info) {
    if (info.subShaders.empty())
        return false;
    std::set<std::string> propertyNames;
    const auto validProperties = [&](const auto& properties) {
        // At most 64 bytes per std140 member, plus final alignment padding.
        if (properties.size() > (std::numeric_limits<std::uint32_t>::max() - 16U) / 64U)
            return false;
        for (const auto& property : properties) {
            if (!validProperty(property) || !propertyNames.insert(property.name).second)
                return false;
        }
        return true;
    };
    if (!validProperties(info.properties) || !validProperties(info.globalProperties))
        return false;
    for (const auto& sub : info.subShaders) {
        if (sub.passes.empty() || sub.renderPipeline.empty())
            return false;
        std::set<std::string> passNames;
        for (const auto& pass : sub.passes) {
            const auto& state = pass.renderState;
            if (pass.name.empty() || !passNames.insert(pass.name).second ||
                !validEnum(pass.type, ShaderPassType::ShadowCaster) ||
                !pass.program.hasSourceProgram() || !validInterface(pass.vertexInput) ||
                !validInterface(pass.varyings) || !validInterface(pass.fragmentOutputs, true) ||
                !validEnum(state.cull, CullMode::Back) ||
                !validEnum(state.frontFace, FrontFace::CounterClockwise) ||
                !validEnum(state.fill, FillMode::Wireframe) ||
                !validEnum(state.topology, PrimitiveTopology::LineList) ||
                !validEnum(state.depthTest, DepthCompare::Always) ||
                !validEnum(state.blend, BlendMode::PremultipliedAlpha) ||
                state.colorMask.find_first_not_of("RGBA") != std::string::npos)
                return false;
            const std::set<std::string> keywords(pass.features.begin(), pass.features.end());
            if (keywords.size() > 64 || !std::ranges::all_of(keywords, [](const auto& keyword) {
                    return validIdentifier(keyword);
                }))
                return false;
        }
    }
    return true;
}

ShaderKeywordSchema::ShaderKeywordSchema(std::vector<std::string> keywords)
    : keywords_(std::move(keywords)) {
    std::ranges::sort(keywords_);
    keywords_.erase(std::unique(keywords_.begin(), keywords_.end()), keywords_.end());
    if (keywords_.size() > 64)
        Log::fatal("ShaderKeywordSchema", "A pass cannot declare more than 64 keywords");
}

bool ShaderKeywordSchema::declares(std::string_view keyword) const {
    return std::ranges::binary_search(keywords_, keyword);
}

std::uint64_t ShaderKeywordSchema::makeKey(std::span<const std::string> enabledKeywords) const {
    std::uint64_t result{};
    for (const auto& keyword : enabledKeywords) {
        const auto it = std::ranges::lower_bound(keywords_, keyword);
        if (it != keywords_.end() && *it == keyword)
            result |= std::uint64_t{1} << std::distance(keywords_.begin(), it);
    }
    return result;
}

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

UniformBlockLayout buildUniformBlockLayout(std::span<const ShaderPropertyInfo> properties) {
    UniformBlockLayout result;
    std::uint32_t cursor = 0;
    for (const ShaderPropertyInfo& property : properties) {
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

ShaderPass::ShaderPass(const ShaderPassCreateInfo& desc)
    : name_(desc.name), type_(desc.type), vertexInput_(desc.vertexInput), varyings_(desc.varyings),
      fragmentOutputs_(desc.fragmentOutputs), renderState_(desc.renderState),
      keyword_(desc.features) {}

Ref<ShaderProgram> ShaderPass::findProgram(std::uint64_t key) const {
    const auto found = programs_.find(key);
    return found == programs_.end() ? Ref<ShaderProgram>{} : found->second;
}

SubShader::SubShader(const SubShaderCreateInfo& desc)
    : renderPipeline_(desc.renderPipeline), renderQueue_(desc.renderQueue) {
    passes_.reserve(desc.passes.size());
    for (const ShaderPassCreateInfo& pass : desc.passes) {
        passes_.emplace_back(pass);
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

Shader::Shader(ShaderCreateInfo info) {
    if (!validateShaderCreateInfo(info))
        Log::fatal("Shader", "Invalid runtime shader description");
    static std::atomic<std::uint64_t> nextId{1};
    instanceId_ = nextId.fetch_add(1, std::memory_order_relaxed);
    name_ = std::move(info.name);
    properties_ = std::move(info.properties);
    globalProperties_ = std::move(info.globalProperties);
    compileKey_ = info.compileKey;
    uniformBlockLayout_ = buildUniformBlockLayout(properties_);
    globalUniformBlockLayout_ = buildUniformBlockLayout(globalProperties_);
    for (const SubShaderCreateInfo& subShader : info.subShaders) {
        subShaders_.emplace_back(subShader);
        auto& sources = programSources_.emplace_back();
        for (const auto& pass : subShader.passes)
            sources.push_back(pass.program);
    }
    GLOBAL_UNIFORM_MANAGER.registerGlobalProperties(globalProperties_);
    liveShaders.insert(this);
}

Shader::~Shader() {
    liveShaders.erase(this);
}

void Shader::clearProgramCaches() {
    for (const auto* shader : liveShaders) {
        for (const auto& sub : shader->subShaders_) {
            for (const auto& pass : sub.passes())
                pass.programs_.clear();
        }
    }
}

ShaderCreateInfo Shader::snapshot() const {
    ShaderCreateInfo info;
    info.name = name_;
    info.properties = properties_;
    info.globalProperties = globalProperties_;
    info.compileKey = compileKey_;
    for (const SubShader& sub : subShaders_) {
        SubShaderCreateInfo subInfo;
        subInfo.renderPipeline = sub.renderPipeline();
        subInfo.renderQueue = sub.renderQueue();
        for (const ShaderPass& pass : sub.passes()) {
            ShaderPassCreateInfo passInfo;
            passInfo.name = pass.name();
            passInfo.type = pass.type();
            passInfo.program = programSource(pass);
            passInfo.vertexInput = pass.vertexInput();
            passInfo.varyings = pass.varyings();
            passInfo.fragmentOutputs = pass.fragmentOutputs();
            passInfo.features = pass.keyword().keywords();
            passInfo.renderState = pass.renderState();
            subInfo.passes.push_back(std::move(passInfo));
        }
        info.subShaders.push_back(std::move(subInfo));
    }
    return info;
}

const ShaderProgramDesc& Shader::programSource(const ShaderPass& pass) const {
    for (std::size_t sub = 0; sub < subShaders_.size(); ++sub) {
        const auto& passes = subShaders_[sub].passes();
        for (std::size_t index = 0; index < passes.size(); ++index) {
            if (&passes[index] == &pass)
                return programSources_[sub][index];
        }
    }
    Log::fatal("Shader", "Pass does not belong to this shader");
}

Ref<Shader> Shader::clone() const {
    return makeRef<Shader>(snapshot());
}

bool Shader::rebuild(ShaderCreateInfo info) {
    if (!validateShaderCreateInfo(info))
        return false;
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        Log::fatal("Shader", "Shader revision overflow");
    Shader replacement{std::move(info)};
    name_ = std::move(replacement.name_);
    properties_ = std::move(replacement.properties_);
    globalProperties_ = std::move(replacement.globalProperties_);
    uniformBlockLayout_ = std::move(replacement.uniformBlockLayout_);
    globalUniformBlockLayout_ = std::move(replacement.globalUniformBlockLayout_);
    subShaders_ = std::move(replacement.subShaders_);
    programSources_ = std::move(replacement.programSources_);
    compileKey_ = replacement.compileKey_;
    ++revision_;
    return true;
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
            return pass.keyword().declares(keyword);
        });
    });
}

void Shader::setGlobalFloat(std::string_view name, float value) {
    GLOBAL_UNIFORM_MANAGER.setFloat(name, value);
}

void Shader::setGlobalInt(std::string_view name, int value) {
    GLOBAL_UNIFORM_MANAGER.setFloat(name, static_cast<float>(value));
}

void Shader::setGlobalVector(std::string_view name, const math::Vec4& value) {
    GLOBAL_UNIFORM_MANAGER.setVector(name, value);
}

void Shader::setGlobalColor(std::string_view name, const math::Vec4& value) {
    GLOBAL_UNIFORM_MANAGER.setColor(name, value);
}

void Shader::setGlobalBool(std::string_view name, bool value) {
    GLOBAL_UNIFORM_MANAGER.setBool(name, value);
}

void Shader::setGlobalMatrix(std::string_view name, const math::Mat44& value) {
    GLOBAL_UNIFORM_MANAGER.setMatrix(name, value);
}

void Shader::setGlobalTexture(std::string_view name, std::string_view texturePath) {
    GLOBAL_UNIFORM_MANAGER.setTexture(name, texturePath);
}

} // namespace engine
