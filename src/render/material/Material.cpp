#include "render/material/Material.h"
#include "render/GraphicsPipelineStorage.h"

#include "core/logging/Log.h"
#include "render/texture/Sampler.h"

#include <algorithm>
#include <atomic>
#include "rhi/api/Device.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include <cassert>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace engine {

namespace {

bool requireType(const UniformMemberLayout& member,
                 ShaderPropertyType expected,
                 std::string_view name) {
    if (member.type != expected) {
        Log::warn("Material",
                  "Property has the wrong type: %.*s",
                  static_cast<int>(name.size()),
                  name.data());
        return false;
    }
    return true;
}

const UniformMemberLayout* findMember(const UniformBlockLayout& layout, std::string_view name) {
    const UniformMemberLayout* member = layout.findMember(name);
    if (!member) {
        Log::warn("Material",
                  "Property does not exist: %.*s",
                  static_cast<int>(name.size()),
                  name.data());
    }
    return member;
}

bool compatiblePropertyTypes(ShaderPropertyType oldType, ShaderPropertyType newType) {
    return oldType == newType;
}

template <typename Value>
Value readUniformValue(const Material& material, const UniformMemberLayout& member) {
    if (member.offset + sizeof(Value) > material.uniformData.size()) {
        Log::fatal("Material", "Uniform layout exceeds its byte buffer");
    }
    Value value{};
    std::memcpy(&value, material.uniformData.data() + member.offset, sizeof(Value));
    return value;
}

template <typename Value>
void writeUniformValue(Material& material, const UniformMemberLayout& member, const Value& value) {
    if (sizeof(Value) > member.size || member.offset + member.size > material.uniformData.size()) {
        Log::fatal("Material", "Uniform layout exceeds its byte buffer");
    }
    std::fill_n(material.uniformData.data() + member.offset, member.size, std::byte{0});
    std::memcpy(material.uniformData.data() + member.offset, &value, sizeof(Value));
}

template <typename Value>
const Value* requireValue(const ShaderValue& value, std::string_view name) {
    if (const Value* typed = std::get_if<Value>(&value)) {
        return typed;
    }
    Log::warn("Material",
              "Value does not match property type: %.*s",
              static_cast<int>(name.size()),
              name.data());
    return nullptr;
}

} // namespace

Ref<Material> Material::errorMaterial;
Material::~Material() = default;

const Shader& Material::shader() const {
    if (!shader_)
        Log::fatal("Material", "Shader reference is null");
    return *shader_;
}

ShaderValue Material::propertyValue(const ShaderPropertyInfo& property) const {
    switch (property.type) {
    case ShaderPropertyType::Float: return getFloat(property.name);
    case ShaderPropertyType::Boolean: return getBool(property.name);
    case ShaderPropertyType::Vec2: return getVec2(property.name);
    case ShaderPropertyType::Vec3: return getVec3(property.name);
    case ShaderPropertyType::Vec4: return getVec4(property.name);
    case ShaderPropertyType::Texture2D: return std::string{};
    case ShaderPropertyType::Matrix: return getMatrix(property.name);
    }
    assert(false && "Unsupported shader property type");
}

Material::Material(MaterialCreateInfo info) {
    static std::atomic<std::uint64_t> nextId{1};
    instanceId_ = nextId.fetch_add(1, std::memory_order_relaxed);
    if (!rebuild(std::move(info)))
        Log::fatal("Material", "Invalid runtime material description");
    version_ = 1;
}

void Material::ensureShaderCurrent() const {
    if (!rebuilding_ && shader_ && shaderRevision_ != shader_->revision())
        const_cast<Material*>(this)->rebuildForShader(shader_, true);
}

bool validateMaterialCreateInfo(const MaterialCreateInfo& info) {
    if (!info.shader)
        return false;
    for (const auto& [name, value] : info.properties) {
        const auto found =
            std::ranges::find(info.shader->properties(), name, &ShaderPropertyInfo::name);
        if (found == info.shader->properties().end() ||
            found->type == ShaderPropertyType::Texture2D ||
            found->defaultValue.index() != value.index())
            return false;
    }
    for (const auto& [name, binding] : info.textures) {
        const auto found =
            std::ranges::find(info.shader->properties(), name, &ShaderPropertyInfo::name);
        if (found == info.shader->properties().end() ||
            found->type != ShaderPropertyType::Texture2D)
            return false;
    }
    return true;
}

bool Material::rebuild(MaterialCreateInfo info) {
    if (!validateMaterialCreateInfo(info))
        return false;
    const auto previousVersion = version_;
    name = std::move(info.name);
    renderQueueOverride_ = info.renderQueue;
    defaultTextureBinding_ = std::move(info.defaultTextureBinding);
    rebuildForShader(std::move(info.shader), false);
    suppressChanges_ = true;
    for (const auto& [name, value] : info.properties)
        setPropertyValue(name, value);
    for (auto& [name, binding] : info.textures)
        textures.insert_or_assign(name, std::move(binding));
    keywords = std::move(info.keywords);
    suppressChanges_ = false;
    version_ = previousVersion;
    markChanged();
    return true;
}

void Material::setShader(Ref<Shader> shader) {
    if (!shader) {
        Log::error("Material", "Shader reference must be valid");
        return;
    }
    if (shader_ == shader && shaderRevision_ == shader->revision())
        return;
    rebuildForShader(std::move(shader), true);
}

void Material::setRenderQueue(std::optional<int> queueOverride) {
    if (renderQueueOverride_ == queueOverride)
        return;
    if (!shader_) {
        Log::error("Material", "setRenderQueue requires a Shader");
        return;
    }
    renderQueueOverride_ = queueOverride;
    renderQueue = renderQueueOverride_.value_or(shader().defaultSubShader().renderQueue());
    markChanged();
}

bool Material::isKeywordEnabled(std::string_view keyword) const {
    return std::ranges::find(keywords, keyword) != keywords.end();
}

std::uint64_t Material::variantKey(const ShaderPass& pass) const {
    return pass.keyword().makeKey(keywords);
}

Ref<ShaderProgram> Material::program(const ShaderPass& pass) const {
    return GRAPHICS_PIPELINE_STORAGE.getOrCreateProgram(shader(), pass, variantKey(pass));
}

void Material::setKeywordEnabled(const std::string& keyword, bool enabled) {
    const auto found = std::ranges::find(keywords, keyword);
    if (enabled == (found != keywords.end()))
        return;
    if (enabled)
        keywords.push_back(keyword);
    else
        keywords.erase(found);
    markChanged();
}

void Material::rebuildForShader(Ref<Shader> newShaderRef, bool preserveValues) {
    if (!newShaderRef) {
        Log::error("Material", "Shader reference must be valid");
        return;
    }
    rebuilding_ = true;
    struct ResetFlag {
        bool& flag;
        ~ResetFlag() { flag = false; }
    } reset{rebuilding_};
    const Shader& newShader = *newShaderRef;

    std::unordered_map<std::string, std::pair<ShaderPropertyType, ShaderValue>> oldValues;
    if (preserveValues && shader_) {
        for (const auto& member : uniformLayout.members) {
            ShaderPropertyInfo property{member.name, member.type};
            oldValues.emplace(member.name, std::pair{member.type, propertyValue(property)});
        }
    }
    auto oldTextures = std::move(textures);
    auto oldKeywords = std::move(keywords);

    shader_ = std::move(newShaderRef);
    shaderRevision_ = newShader.revision();
    uniformLayout = newShader.uniformBlockLayout();
    uniformData.assign(uniformLayout.byteSize, std::byte{0});
    textures.clear();
    bindings_.clear();
    keywords.clear();
    renderQueue = renderQueueOverride_.value_or(newShader.defaultSubShader().renderQueue());

    suppressChanges_ = true;
    for (const auto& property : newShader.properties()) {
        if (property.type == ShaderPropertyType::Texture2D) {
            const auto old = oldTextures.find(property.name);
            textures[property.name] =
                preserveValues && old != oldTextures.end()
                    ? std::move(old->second)
                    : (defaultTextureBinding_ ? defaultTextureBinding_(property)
                                              : MaterialTextureBinding{});
            continue;
        }
        setPropertyValue(property.name, property.defaultValue);
        const auto old = oldValues.find(property.name);
        if (old != oldValues.end() && compatiblePropertyTypes(old->second.first, property.type))
            setPropertyValue(property.name, old->second.second);
    }
    keywords = std::move(oldKeywords);
    suppressChanges_ = false;
    if (preserveValues)
        markChanged();
    else {
        version_ = 1;
        dirty_ = true;
    }
}

float Material::getFloat(std::string_view name) const {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Float, name)
               ? readUniformValue<float>(*this, *member)
               : 0.0F;
}

math::Vec2 Material::getVec2(std::string_view name) const {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Vec2, name)
               ? readUniformValue<math::Vec2>(*this, *member)
               : math::Vec2{};
}

math::Vec3 Material::getVec3(std::string_view name) const {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Vec3, name)
               ? readUniformValue<math::Vec3>(*this, *member)
               : math::Vec3{};
}

math::Vec4 Material::getVec4(std::string_view name) const {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Vec4, name)
               ? readUniformValue<math::Vec4>(*this, *member)
               : math::Vec4{};
}

math::Mat44 Material::getMatrix(std::string_view name) const {
    ensureShaderCurrent();
    const auto* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Matrix, name)
               ? readUniformValue<math::Mat44>(*this, *member)
               : math::Mat44{1.0F};
}

void Material::setMatrix(std::string_view name, const math::Mat44& value) {
    ensureShaderCurrent();
    const auto* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Matrix, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

bool Material::getBool(std::string_view name) const {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Boolean, name) &&
           readUniformValue<std::uint32_t>(*this, *member) != 0;
}

Ref<Texture> Material::getTexture(std::string_view name) const {
    return resolveTexture(name);
}
Ref<Texture> Material::resolveTexture(std::string_view name) const {
    ensureShaderCurrent();
    const auto found = textures.find(std::string{name});
    if (found == textures.end())
        return {};
    auto& binding = found->second;
    if (!binding.texture && binding.resolve)
        binding.texture = binding.resolve();
    return binding.texture;
}
Ref<Sampler> Material::resolveSampler(std::string_view name) const {
    ensureShaderCurrent();
    const auto found = textures.find(std::string{name});
    return found == textures.end() ? Ref<Sampler>{} : found->second.sampler;
}

void Material::setFloat(std::string_view name, float value) {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Float, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec2(std::string_view name, const math::Vec2& value) {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec2, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec3(std::string_view name, const math::Vec3& value) {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec3, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec4(std::string_view name, const math::Vec4& value) {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec4, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setBool(std::string_view name, bool value) {
    ensureShaderCurrent();
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Boolean, name))
        return;
    const std::uint32_t encoded = value ? 1U : 0U;
    writeUniformValue(*this, *member, encoded);
    markChanged();
}

void Material::setTexture(std::string_view name, MaterialTextureBinding binding) {
    ensureShaderCurrent();
    const auto found = textures.find(std::string{name});
    if (found == textures.end())
        return;
    found->second = std::move(binding);
    markChanged();
}
void Material::setTexture(std::string_view name, Ref<Texture> texture) {
    setTexture(name, MaterialTextureBinding{std::move(texture)});
}
void Material::setTexture(std::string_view name,
                          Ref<Texture> texture,
                          const Ref<Sampler>& sampler) {
    setTexture(name, MaterialTextureBinding{std::move(texture), sampler});
}

void Material::setPropertyValue(std::string_view name, const ShaderValue& value) {
    if (const UniformMemberLayout* member = uniformLayout.findMember(name)) {
        switch (member->type) {
        case ShaderPropertyType::Float: {
            if (const float* typed = requireValue<float>(value, name)) {
                setFloat(name, *typed);
            }
            return;
        }
        case ShaderPropertyType::Vec2: {
            if (const math::Vec2* typed = requireValue<math::Vec2>(value, name)) {
                setVec2(name, *typed);
            }
            return;
        }
        case ShaderPropertyType::Vec3: {
            if (const math::Vec3* typed = requireValue<math::Vec3>(value, name)) {
                setVec3(name, *typed);
            }
            return;
        }
        case ShaderPropertyType::Vec4: {
            if (const math::Vec4* typed = requireValue<math::Vec4>(value, name)) {
                setVec4(name, *typed);
            }
            return;
        }
        case ShaderPropertyType::Boolean: {
            if (const bool* typed = requireValue<bool>(value, name)) {
                setBool(name, *typed);
            }
            return;
        }
        case ShaderPropertyType::Texture2D: break;
        case ShaderPropertyType::Matrix:
            if (const auto* typed = requireValue<math::Mat44>(value, name)) {
                setMatrix(name, *typed);
            }
            return;
        }
    }
}

void Material::markChanged() {
    if (suppressChanges_) {
        return;
    }
    if (version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::fatal("Material", "Version overflow");
    }
    ++version_;
    dirty_ = true;
}

Ref<Material> Material::clone() const {
    ensureShaderCurrent();
    MaterialCreateInfo info;
    info.name = name;
    info.shader = shader_;
    info.keywords = keywords;
    info.renderQueue = renderQueueOverride_;
    info.defaultTextureBinding = defaultTextureBinding_;
    info.textures = textures;
    for (const auto& property : shader_->properties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            info.properties.emplace(property.name, propertyValue(property));
    }
    return makeRef<Material>(std::move(info));
}

Ref<UniformSet> Material::uniformSet(const ShaderProgram& program, std::uint32_t frameIndex) const {
    ensureShaderCurrent();
    auto* device = RHI_DEVICE;
    if (!device || program.deviceUid() != device->uid() || program.setCount() < 2 ||
        program.bindings(1).empty())
        return {};
    if (deviceUid_ != device->uid()) {
        bindings_.clear();
        deviceUid_ = device->uid();
        for (auto& [name, binding] : textures)
            if (binding.resolve)
                binding.texture.reset();
    }
    const auto& descriptions = program.bindings(1);
    std::vector<rhi::TextureBinding> signature;
    for (const auto& desc : descriptions) {
        if (desc.type == rhi::ResourceType::CombinedImageSampler) {
            const auto texture = resolveTexture(desc.name);
            const auto sampler = resolveSampler(desc.name);
            const auto binding = texture ? texture->binding(sampler) : rhi::TextureBinding{};
            if (!binding.view || !binding.sampler)
                return {};
            signature.push_back(binding);
        } else if (desc.type != rhi::ResourceType::UniformBuffer)
            return {};
    }
    auto& entry = bindings_[frameIndex][program.id()];
    const auto sameTextures = [&] {
        if (signature.size() != entry.textures.size())
            return false;
        for (std::size_t i = 0; i < signature.size(); ++i)
            if (signature[i].view != entry.textures[i].view ||
                signature[i].sampler != entry.textures[i].sampler)
                return false;
        return true;
    };
    const bool changed = entry.version != version_ || !sameTextures();
    if (!entry.set || (changed && entry.set->refCount() > 1)) {
        entry.set = makeRef<UniformSet>(program.setLayout(1), program.uboLayouts(1));
        entry.version = 0;
    }
    if (entry.version != version_ || changed) {
        std::size_t uboIndex = 0;
        for (std::size_t position = 0; position < descriptions.size(); ++position) {
            const auto& desc = descriptions[position];
            if (desc.type == rhi::ResourceType::UniformBuffer) {
                if (uboIndex >= program.uboLayouts(1).size())
                    return {};
                auto* ubo = entry.set->uboAt(static_cast<std::uint32_t>(position));
                if (!ubo)
                    return {};
                const auto& layout = program.uboLayouts(1)[uboIndex++];
                // Bulk-copy only when every reflected member matches the CPU layout.
                const bool identical =
                    layout->byteSize() == uniformLayout.byteSize &&
                    layout->uniforms().size() == uniformLayout.members.size() &&
                    std::ranges::all_of(layout->uniforms(), [&](const UboUniform& reflected) {
                        const auto* member = uniformLayout.findMember(reflected.name);
                        if (!member || member->offset != reflected.offset ||
                            member->size != reflected.size)
                            return false;
                        switch (member->type) {
                        case ShaderPropertyType::Float:
                            return reflected.type == UboUniformType::Float;
                        case ShaderPropertyType::Boolean:
                            return reflected.type == UboUniformType::Bool ||
                                   reflected.type == UboUniformType::UInt;
                        case ShaderPropertyType::Vec2:
                            return reflected.type == UboUniformType::Vec2;
                        case ShaderPropertyType::Vec3:
                            return reflected.type == UboUniformType::Vec3;
                        case ShaderPropertyType::Vec4:
                            return reflected.type == UboUniformType::Vec4;
                        case ShaderPropertyType::Matrix:
                            return reflected.type == UboUniformType::Mat4;
                        case ShaderPropertyType::Texture2D: return false;
                        }
                        return false;
                    });
                if (identical) {
                    ubo->setRawBytes(uniformData);
                    continue;
                }
                for (const auto& member : layout->uniforms()) {
                    const auto property = std::ranges::find(
                        shader_->properties(), member.name, &ShaderPropertyInfo::name);
                    if (property == shader_->properties().end() ||
                        property->type == ShaderPropertyType::Texture2D ||
                        !ubo->setUniform(member.name, propertyValue(*property))) {
                        Log::error(
                            "Material", "Incompatible reflected member: %s", member.name.c_str());
                        return {};
                    }
                }
            } else {
                if (!entry.set->setResourceAt(static_cast<std::uint32_t>(position),
                                              resolveTexture(desc.name)) ||
                    !entry.set->setSamplerOverrideAt(static_cast<std::uint32_t>(position),
                                                     resolveSampler(desc.name)))
                    return {};
            }
        }
        entry.version = version_;
        entry.textures = std::move(signature);
    }
    entry.set->upload();
    return entry.set;
}

} // namespace engine
