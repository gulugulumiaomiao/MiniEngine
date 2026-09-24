#include "render/material/Material.h"

#include "asset/types/MaterialAsset.h"

#include "asset/database/AssetDatabase.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "render/material/MaterialManager.h"
#include "render/shader/ShaderManager.h"
#include "render/texture/Sampler.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace engine {

namespace {

constexpr std::uint32_t kMaterialAssetMagic = 0x4c54414dU;
constexpr std::uint16_t kMaterialAssetVersion = 2;

struct MaterialPropertyEntry : public Transferable {
    std::string name;
    ShaderValue value;

    MaterialPropertyEntry() = default;
    MaterialPropertyEntry(std::string name, ShaderValue value)
        : name(std::move(name)), value(std::move(value)) {}

    [[nodiscard]] bool transfer(Transfer& archive) override {
        return archive.beginObject({}) && archive.transfer("name", name) &&
               archive.transfer("value", value) && archive.endObject();
    }
};

bool transferMaterialPayload(Transfer& archive, MaterialAsset& value) {
    std::uint32_t magic = kMaterialAssetMagic;
    std::uint16_t version = kMaterialAssetVersion;
    if (!archive.transfer("magic", magic) || magic != kMaterialAssetMagic ||
        !archive.transfer("version", version) || version != kMaterialAssetVersion ||
        !archive.transfer("name", value.name) || !archive.transfer("shader", value.shader)) {
        return false;
    }

    std::vector<MaterialPropertyEntry> entries;
    if (archive.writing()) {
        entries.reserve(value.properties.size());
        for (const auto& [name, property] : value.properties) {
            entries.push_back({name, property});
        }
        std::ranges::sort(entries, {}, &MaterialPropertyEntry::name);
    }
    if (!archive.transfer("properties", entries))
        return false;
    if (archive.reading()) {
        value.properties.clear();
        for (MaterialPropertyEntry& entry : entries) {
            if (!value.properties.emplace(std::move(entry.name), std::move(entry.value)).second) {
                return false;
            }
        }
    }
    return archive.transfer("keywords", value.keywords) &&
           archive.transfer("render_queue", value.renderQueue);
}

} // namespace

bool MaterialAsset::transfer(Transfer& archive) {
    MaterialAsset decoded;
    MaterialAsset& target = archive.reading() ? decoded : *this;
    if (!archive.beginObject({}) || !transferMaterialPayload(archive, target) ||
        !archive.endObject()) {
        Log::error("MaterialAsset",
                   "Invalid payload %s: %s",
                   assetPath().string().c_str(),
                   archive.error().empty() ? "validation failed" : archive.error().c_str());
        return false;
    }
    if (archive.reading()) {
        name = std::move(decoded.name);
        shader = std::move(decoded.shader);
        properties = std::move(decoded.properties);
        keywords = std::move(decoded.keywords);
        renderQueue = decoded.renderQueue;
    }
    return true;
}

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

bool requireOneOf(const UniformMemberLayout& member,
                  ShaderPropertyType first,
                  ShaderPropertyType second,
                  std::string_view name) {
    if (member.type != first && member.type != second) {
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
    if (oldType == newType) {
        return true;
    }
    const bool scalarPair =
        (oldType == ShaderPropertyType::Float || oldType == ShaderPropertyType::Range) &&
        (newType == ShaderPropertyType::Float || newType == ShaderPropertyType::Range);
    const bool vec4Pair =
        (oldType == ShaderPropertyType::Vec4 || oldType == ShaderPropertyType::Color) &&
        (newType == ShaderPropertyType::Vec4 || newType == ShaderPropertyType::Color);
    return scalarPair || vec4Pair;
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

Material::~Material() {
    MATERIAL_RESOURCE_MANAGER.unregister(this);
}

const Shader& Material::shader() const {
    if (!shader_)
        Log::fatal("Material", "Shader reference is null");
    return *shader_;
}

ShaderValue Material::propertyValue(const ShaderPropertyDesc& property) const {
    switch (property.type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range: return getFloat(property.name);
    case ShaderPropertyType::Boolean: return getBool(property.name);
    case ShaderPropertyType::Vec2: return getVec2(property.name);
    case ShaderPropertyType::Vec3: return getVec3(property.name);
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color: return getVec4(property.name);
    case ShaderPropertyType::Texture2D: return getTexture(property.name);
    case ShaderPropertyType::Matrix: return math::Mat44{1.0F};
    }
    assert(false && "Unsupported shader property type");
}

void Material::initialize(AssetId assetId,
                          VirtualPath assetPath,
                          std::string materialName,
                          Ref<Shader> shader,
                          std::optional<int> renderQueueOverride) {
    assetId_ = assetId;
    assetPath_ = std::move(assetPath);
    name = std::move(materialName);
    renderQueueOverride_ = renderQueueOverride;
    if (!shader) {
        Log::error("Material", "Shader reference must be valid");
        return;
    }
    rebuildForShader(std::move(shader), false);
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

void Material::setKeywordEnabled(const std::string& keyword, bool enabled) {
    const auto found = std::ranges::find(keywords, keyword);
    if (enabled == (found != keywords.end()))
        return;
    if (!shader_ || !shader().declaresKeyword(keyword)) {
        Log::warn("Material", "Shader does not declare keyword: %s", keyword.c_str());
        return;
    }
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
    const Shader& newShader = *newShaderRef;

    std::unordered_map<std::string, std::pair<ShaderPropertyType, ShaderValue>> oldValues;
    if (preserveValues && shader_) {
        for (const ShaderPropertyDesc& property : shader().properties())
            oldValues.emplace(property.name, std::pair{property.type, propertyValue(property)});
    }
    const auto oldTextureRefs = std::move(textureRefs_);
    const auto oldKeywords = std::move(keywords);

    shader_ = std::move(newShaderRef);
    shaderRevision_ = newShader.revision();
    uniformLayout = newShader.uniformBlockLayout();
    uniformData.assign(uniformLayout.byteSize, std::byte{0});
    textures.clear();
    textureRefs_.clear();
    keywords.clear();
    renderQueue = renderQueueOverride_.value_or(newShader.defaultSubShader().renderQueue());

    suppressChanges_ = true;
    for (const ShaderPropertyDesc& property : newShader.properties()) {
        setPropertyValue(property.name, property.defaultValue);
        const auto old = oldValues.find(property.name);
        if (old != oldValues.end() && compatiblePropertyTypes(old->second.first, property.type))
            setPropertyValue(property.name, old->second.second);
        if (preserveValues && property.type == ShaderPropertyType::Texture2D) {
            if (const auto texture = oldTextureRefs.find(property.name);
                texture != oldTextureRefs.end())
                textureRefs_.insert(*texture);
        }
    }
    for (const std::string& keyword : oldKeywords) {
        if (newShader.declaresKeyword(keyword))
            keywords.push_back(keyword);
    }
    suppressChanges_ = false;
    if (preserveValues)
        markChanged();
    else {
        version_ = 1;
        dirty_ = true;
    }
}

Ref<Material> MaterialAsset::instantiate(const Ref<Shader>& shader) const {
    Ref<Material> material = makeRef<Material>();
    const AssetId assetId = ASSET_DATABASE.findGuid(assetPath()).value_or(AssetId{});
    material->initialize(assetId, assetPath(), name, shader, renderQueue);
    if (!shader)
        return material;
    for (const std::string& keyword : keywords) {
        if (!shader->declaresKeyword(keyword)) {
            Log::warn("Material",
                      "Keyword is not declared by shader: %s (%s)",
                      keyword.c_str(),
                      name.c_str());
        }
    }
    material->keywords = keywords;
    material->suppressChanges_ = true;
    for (const auto& [propertyName, value] : properties)
        material->setPropertyValue(propertyName, value);
    material->suppressChanges_ = false;
    material->version_ = 1;
    material->dirty_ = true;
    return material;
}

float Material::getFloat(std::string_view name) const {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member &&
                   requireOneOf(*member, ShaderPropertyType::Float, ShaderPropertyType::Range, name)
               ? readUniformValue<float>(*this, *member)
               : 0.0F;
}

math::Vec2 Material::getVec2(std::string_view name) const {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Vec2, name)
               ? readUniformValue<math::Vec2>(*this, *member)
               : math::Vec2{};
}

math::Vec3 Material::getVec3(std::string_view name) const {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Vec3, name)
               ? readUniformValue<math::Vec3>(*this, *member)
               : math::Vec3{};
}

math::Vec4 Material::getVec4(std::string_view name) const {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member &&
                   requireOneOf(*member, ShaderPropertyType::Vec4, ShaderPropertyType::Color, name)
               ? readUniformValue<math::Vec4>(*this, *member)
               : math::Vec4{};
}

bool Material::getBool(std::string_view name) const {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireType(*member, ShaderPropertyType::Boolean, name) &&
           readUniformValue<std::uint32_t>(*this, *member) != 0;
}

const std::string& Material::getTexture(std::string_view name) const {
    const auto texture = textures.find(std::string{name});
    if (texture == textures.end()) {
        Log::warn("Material",
                  "Texture property does not exist: %.*s",
                  static_cast<int>(name.size()),
                  name.data());
        static const std::string empty;
        return empty;
    }
    return texture->second;
}

Ref<Texture> Material::resolveTexture(std::string_view name) const {
    const std::string key{name};
    const auto existing = textureRefs_.find(key);
    if (existing != textureRefs_.end())
        return existing->second;
    const auto path = textures.find(key);
    if (path == textures.end())
        return {};
    Ref<Texture> texture = resolveTextureReference(path->second);
    if (texture)
        textureRefs_.insert_or_assign(key, texture);
    return texture;
}

Ref<Sampler> Material::resolveSampler(std::string_view name) const {
    const auto existing = textureSamplers_.find(std::string{name});
    return existing != textureSamplers_.end() ? existing->second : Ref<Sampler>{};
}

void Material::setFloat(std::string_view name, float value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member ||
        !requireOneOf(*member, ShaderPropertyType::Float, ShaderPropertyType::Range, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec2(std::string_view name, const math::Vec2& value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec2, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec3(std::string_view name, const math::Vec3& value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec3, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec4(std::string_view name, const math::Vec4& value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member ||
        !requireOneOf(*member, ShaderPropertyType::Vec4, ShaderPropertyType::Color, name))
        return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setBool(std::string_view name, bool value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Boolean, name))
        return;
    const std::uint32_t encoded = value ? 1U : 0U;
    writeUniformValue(*this, *member, encoded);
    markChanged();
}

void Material::setTexture(std::string_view name, std::string value) {
    const auto texture = textures.find(std::string{name});
    if (texture == textures.end()) {
        Log::warn("Material",
                  "Texture property does not exist: %.*s",
                  static_cast<int>(name.size()),
                  name.data());
        return;
    }
    texture->second = std::move(value);
    textureRefs_.erase(std::string{name});
    markChanged();
}

void Material::setTexture(std::string_view name, Ref<Texture> texture) {
    if (!textures.contains(std::string{name}) || !texture) {
        Log::warn("Material",
                  "Cannot bind an invalid Texture: %.*s",
                  static_cast<int>(name.size()),
                  name.data());
        return;
    }
    const std::string key{name};
    textures[key] = texture->isAssetBacked() ? texture->assetPath().string() : std::string{};
    textureRefs_.insert_or_assign(key, std::move(texture));
    markChanged();
}

void Material::setTexture(std::string_view name, Ref<Texture> texture, const Ref<Sampler>& sampler) {
    setTexture(name, std::move(texture));
    if (sampler)
        textureSamplers_.insert_or_assign(std::string{name}, sampler);
}

void Material::setPropertyValue(std::string_view name, const ShaderValue& value) {
    if (const UniformMemberLayout* member = uniformLayout.findMember(name)) {
        switch (member->type) {
        case ShaderPropertyType::Float:
        case ShaderPropertyType::Range: {
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
        case ShaderPropertyType::Vec4:
        case ShaderPropertyType::Color: {
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
        case ShaderPropertyType::Matrix: break;
        }
    }
    if (const std::string* typed = requireValue<std::string>(value, name)) {
        textureRefs_.erase(std::string{name});
        textures.insert_or_assign(std::string{name}, *typed);
        markChanged();
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
    Ref<Material> copy = makeRef<Material>();
    copy->assetPath_ = assetPath_;
    copy->name = name;
    copy->uniformLayout = uniformLayout;
    copy->uniformData = uniformData;
    copy->textures = textures;
    copy->textureRefs_ = textureRefs_;
    copy->keywords = keywords;
    copy->renderQueue = renderQueue;
    copy->shader_ = shader_;
    copy->shaderRevision_ = shaderRevision_;
    copy->renderQueueOverride_ = renderQueueOverride_;
    copy->dirty_ = true;
    copy->version_ = version_;
    return copy;
}

void Material::rebuildFromAsset(const MaterialAsset& asset, Ref<Shader> newShader) {
    if (!newShader) {
        Log::error("Material", "Shader reference must be valid");
        return;
    }

    // Keep identity in sync with the latest database record.
    if (const auto resolvedId = ASSET_DATABASE.findGuid(asset.assetPath())) {
        assetId_ = *resolvedId;
    }
    assetPath_ = asset.assetPath();
    name = asset.name;
    renderQueueOverride_ = asset.renderQueue;

    // Rebuild layout if the shader changed, preserving compatible overrides.
    if (shader_ != newShader || shaderRevision_ != newShader->revision())
        rebuildForShader(newShader, true);

    // Apply authoritative asset values.
    suppressChanges_ = true;
    for (const auto& [propertyName, value] : asset.properties) {
        setPropertyValue(propertyName, value);
    }

    keywords.clear();
    for (const std::string& keyword : asset.keywords) {
        if (newShader->declaresKeyword(keyword)) {
            keywords.push_back(keyword);
        } else {
            Log::warn("Material",
                      "Keyword is not declared by shader: %s (%s)",
                      keyword.c_str(),
                      name.c_str());
        }
    }
    suppressChanges_ = false;

    renderQueue = renderQueueOverride_.value_or(newShader->defaultSubShader().renderQueue());
    markChanged();
}

} // namespace engine
