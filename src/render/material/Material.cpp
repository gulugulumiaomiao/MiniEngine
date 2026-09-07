#include "render/material/Material.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "asset/manager/AssetManager.h"

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

struct MaterialPropertyEntry {
    std::string name;
    ShaderValue value;

    [[nodiscard]] bool transfer(Transfer& archive) {
        return archive.transfer("name", name) && archive.transfer("value", value);
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

const Shader& Material::shader() const {
    const Shader* shader = SHADER_MANAGER.find(shaderHandle_);
    if (!shader)
        Log::fatal("Material", "Invalid or stale ShaderHandle");
    return *shader;
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
    }
    assert(false && "Unsupported shader property type");
}

void Material::initialize(VirtualPath assetPath,
                          std::string materialName,
                          ShaderHandle shader,
                          std::optional<int> renderQueueOverride) {
    assetPath_ = std::move(assetPath);
    name = std::move(materialName);
    renderQueueOverride_ = renderQueueOverride;
    if (!SHADER_MANAGER.find(shader)) {
        Log::error("Material", "ShaderHandle must be valid");
        return;
    }
    rebuildForShader(shader, false);
}

void Material::setShader(ShaderHandle shader) {
    const Shader* value = SHADER_MANAGER.find(shader);
    if (!value) {
        Log::error("Material", "ShaderHandle must be valid");
        return;
    }
    if (shaderHandle_ == shader && shaderRevision_ == value->revision()) {
        return;
    }
    rebuildForShader(shader, true);
}

void Material::rebuildForShader(ShaderHandle newShaderHandle, bool preserveValues) {
    const Shader* newShaderValue = SHADER_MANAGER.find(newShaderHandle);
    if (!newShaderValue) {
        Log::error("Material", "ShaderHandle must be valid");
        return;
    }
    const Shader& newShader = *newShaderValue;

    std::unordered_map<std::string, std::pair<ShaderPropertyType, ShaderValue>> oldValues;
    if (preserveValues && SHADER_MANAGER.find(shaderHandle_)) {
        for (const ShaderPropertyDesc& property : shader().properties()) {
            oldValues.emplace(property.name, std::pair{property.type, propertyValue(property)});
        }
    }

    Material replacement;
    replacement.assetPath_ = assetPath_;
    replacement.name = name;
    replacement.shaderHandle_ = newShaderHandle;
    replacement.shaderRevision_ = newShader.revision();
    replacement.renderQueueOverride_ = renderQueueOverride_;
    replacement.uniformLayout = newShader.uniformBlockLayout();
    replacement.uniformData.resize(replacement.uniformLayout.byteSize, std::byte{0});
    replacement.renderQueue =
        replacement.renderQueueOverride_.value_or(newShader.defaultSubShader().renderQueue());
    replacement.suppressChanges_ = true;
    for (const ShaderPropertyDesc& property : newShader.properties()) {
        replacement.setPropertyValue(property.name, property.defaultValue);
        const auto old = oldValues.find(property.name);
        if (old != oldValues.end() && compatiblePropertyTypes(old->second.first, property.type)) {
            replacement.setPropertyValue(property.name, old->second.second);
        }
    }
    for (const std::string& keyword : keywords) {
        if (newShader.declaresKeyword(keyword)) {
            replacement.keywords.push_back(keyword);
        }
    }
    replacement.suppressChanges_ = false;
    if (preserveValues) {
        replacement.version_ = version_;
        replacement.dirty_ = dirty_;
        replacement.markChanged();
    } else {
        replacement.version_ = 1;
        replacement.dirty_ = true;
    }
    *this = std::move(replacement);
}

Material MaterialAsset::instantiate(ShaderHandle shaderHandle) const {
    Material material;
    material.initialize(assetPath(), name, shaderHandle, renderQueue);
    if (!SHADER_MANAGER.find(shaderHandle))
        return material;
    material.keywords = keywords;
    material.suppressChanges_ = true;
    for (const auto& [propertyName, value] : properties) {
        material.setPropertyValue(propertyName, value);
    }
    material.suppressChanges_ = false;
    material.version_ = 1;
    material.dirty_ = true;
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
    markChanged();
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
        }
    }
    if (const std::string* typed = requireValue<std::string>(value, name)) {
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

MaterialHandle MaterialManager::load(const VirtualPath& materialPath) {
    if (!materialPath.valid()) {
        Log::error("MaterialManager", "Invalid Material path: %s", materialPath.string().c_str());
        return {};
    }
    if (const MaterialHandle existing = handleFor(materialPath); existing) {
        return existing;
    }
    Log::info("Material", "Loading material: %s", materialPath.string().c_str());
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    if (!asset) {
        return {};
    }
    const ShaderHandle shader = SHADER_MANAGER.load(asset->shader);
    if (!shader)
        return {};
    return insert(asset->instantiate(shader));
}

bool MaterialManager::validate(const Material& material) const {
    if (!SHADER_MANAGER.find(material.shaderHandle())) {
        Log::error("MaterialManager", "Material has an invalid ShaderHandle");
        return false;
    }
    return true;
}

void MaterialManager::setShader(MaterialHandle handle, const VirtualPath& shaderPath) {
    Material* material = find(handle);
    if (!material) {
        Log::error("MaterialManager", "Cannot set Shader on an invalid Material");
        return;
    }
    const ShaderHandle shader = SHADER_MANAGER.load(shaderPath);
    if (!shader) {
        Log::error("MaterialManager", "Shader failed to load: %s", shaderPath.string().c_str());
        return;
    }
    material->setShader(shader);
}

void MaterialManager::refreshShader(const VirtualPath& shaderPath) {
    forEach([&shaderPath](Material& material) {
        if (material.shader().assetPath() == shaderPath) {
            material.setShader(material.shaderHandle());
        }
    });
}

} // namespace engine
