#include "renderer/Material.h"

#include "core/Log.h"
#include "renderer/AssetManager.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace engine {
namespace {

bool requireType(const UniformMemberLayout& member, ShaderPropertyType expected,
                 std::string_view name) {
    if (member.type != expected) {
        Log::warn("Material", "Property has the wrong type: %.*s",
                  static_cast<int>(name.size()), name.data());
        return false;
    }
    return true;
}

bool requireOneOf(const UniformMemberLayout& member, ShaderPropertyType first,
                  ShaderPropertyType second, std::string_view name) {
    if (member.type != first && member.type != second) {
        Log::warn("Material", "Property has the wrong type: %.*s",
                  static_cast<int>(name.size()), name.data());
        return false;
    }
    return true;
}

const UniformMemberLayout* findMember(const UniformBlockLayout& layout,
                                      std::string_view name) {
    const UniformMemberLayout* member = layout.findMember(name);
    if (!member) {
        Log::warn("Material", "Property does not exist: %.*s",
                  static_cast<int>(name.size()), name.data());
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
void writeUniformValue(Material& material, const UniformMemberLayout& member,
                       const Value& value) {
    if (sizeof(Value) > member.size ||
        member.offset + member.size > material.uniformData.size()) {
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
    Log::warn("Material", "Value does not match property type: %.*s",
              static_cast<int>(name.size()), name.data());
    return nullptr;
}

} // namespace

const Shader& Material::shader() const {
    if (!shader_) {
        Log::fatal("Material", "Has no Shader");
    }
    return *shader_;
}

ShaderValue Material::propertyValue(const ShaderPropertyDesc& property) const {
    switch (property.type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range:
        return getFloat(property.name);
    case ShaderPropertyType::Boolean:
        return getBool(property.name);
    case ShaderPropertyType::Vec2:
        return getVec2(property.name);
    case ShaderPropertyType::Vec3:
        return getVec3(property.name);
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color:
        return getVec4(property.name);
    case ShaderPropertyType::Texture2D:
        return getTexture(property.name);
    }
    assert(false && "Unsupported shader property type");
}

void Material::initialize(std::string materialName,
                          std::shared_ptr<Shader> shader,
                          std::optional<int> renderQueueOverride) {
    name = std::move(materialName);
    renderQueueOverride_ = renderQueueOverride;
    if (!shader) {
        Log::error("Material", "Shader must not be null");
        return;
    }
    rebuildForShader(std::move(shader), false);
}

void Material::setShader(std::shared_ptr<Shader> shader) {
    if (!shader) {
        Log::error("Material", "Shader must not be null");
        return;
    }
    if (shader_ == shader) {
        return;
    }
    rebuildForShader(std::move(shader), true);
}

void Material::rebuildForShader(std::shared_ptr<Shader> newShader,
                                bool preserveValues) {
    assert(newShader && "Shader must not be null");

    std::unordered_map<std::string, std::pair<ShaderPropertyType, ShaderValue>> oldValues;
    if (preserveValues && shader_) {
        for (const ShaderPropertyDesc& property : shader_->properties()) {
            oldValues.emplace(property.name,
                              std::pair{property.type, propertyValue(property)});
        }
    }

    Material replacement;
    replacement.name = name;
    replacement.shader_ = std::move(newShader);
    replacement.renderQueueOverride_ = renderQueueOverride_;
    replacement.uniformLayout = replacement.shader_->uniformBlockLayout();
    replacement.uniformData.resize(replacement.uniformLayout.byteSize, std::byte{0});
    replacement.renderQueue = replacement.renderQueueOverride_.value_or(
        replacement.shader_->defaultSubShader().renderQueue());
    replacement.suppressChanges_ = true;
    for (const ShaderPropertyDesc& property : replacement.shader_->properties()) {
        replacement.setPropertyValue(property.name, property.defaultValue);
        const auto old = oldValues.find(property.name);
        if (old != oldValues.end() &&
            compatiblePropertyTypes(old->second.first, property.type)) {
            replacement.setPropertyValue(property.name, old->second.second);
        }
    }
    for (const std::string& keyword : keywords) {
        if (replacement.shader_->declaresKeyword(keyword)) {
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

float Material::getFloat(std::string_view name) const {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    return member && requireOneOf(*member, ShaderPropertyType::Float,
                                  ShaderPropertyType::Range, name)
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
    return member && requireOneOf(*member, ShaderPropertyType::Vec4,
                                  ShaderPropertyType::Color, name)
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
        Log::warn("Material", "Texture property does not exist: %.*s",
                  static_cast<int>(name.size()), name.data());
        static const std::string empty;
        return empty;
    }
    return texture->second;
}

void Material::setFloat(std::string_view name, float value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireOneOf(*member, ShaderPropertyType::Float,
                                 ShaderPropertyType::Range, name)) return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec2(std::string_view name, const math::Vec2& value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec2, name)) return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec3(std::string_view name, const math::Vec3& value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Vec3, name)) return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setVec4(std::string_view name, const math::Vec4& value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireOneOf(*member, ShaderPropertyType::Vec4,
                                 ShaderPropertyType::Color, name)) return;
    writeUniformValue(*this, *member, value);
    markChanged();
}

void Material::setBool(std::string_view name, bool value) {
    const UniformMemberLayout* member = findMember(uniformLayout, name);
    if (!member || !requireType(*member, ShaderPropertyType::Boolean, name)) return;
    const std::uint32_t encoded = value ? 1U : 0U;
    writeUniformValue(*this, *member, encoded);
    markChanged();
}

void Material::setTexture(std::string_view name, std::string value) {
    const auto texture = textures.find(std::string{name});
    if (texture == textures.end()) {
        Log::warn("Material", "Texture property does not exist: %.*s",
                  static_cast<int>(name.size()), name.data());
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
        case ShaderPropertyType::Texture2D:
            break;
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

MaterialManager& MaterialManager::instance() {
    static MaterialManager manager;
    return manager;
}

MaterialHandle MaterialManager::createInstance(
    const std::filesystem::path& materialPath) {
    Log::info("Material", "Loading material: %s", materialPath.string().c_str());
    return createInstance(ASSET_MANAGER.loadMaterialAsset(materialPath));
}

MaterialHandle MaterialManager::createInstance(
    std::shared_ptr<MaterialAsset> asset) {
    if (!asset) {
        Log::error("MaterialManager", "MaterialAsset must not be null");
        return {};
    }
    if (!asset->shaderAsset) {
        Log::error("MaterialManager", "MaterialAsset has no ShaderAsset: %s",
                   asset->assetPath().string().c_str());
        return {};
    }
    if (!validateMaterialAsset(*asset, *asset->shaderAsset,
                               asset->assetPath())) {
        return {};
    }
    auto slot = std::ranges::find_if(materials_, [](const Slot& candidate) {
        return !candidate.alive;
    });
    if (slot == materials_.end()) {
        materials_.emplace_back();
        slot = std::prev(materials_.end());
    }

    Material material;
    material.initialize(asset->name, std::make_shared<Shader>(*asset->shaderAsset),
                        asset->renderQueue);
    material.keywords = asset->keywords;
    material.suppressChanges_ = true;
    for (const auto& [name, value] : asset->properties) {
        material.setPropertyValue(name, value);
    }
    material.suppressChanges_ = false;
    material.version_ = 1;
    material.dirty_ = true;
    slot->material = std::move(material);
    slot->alive = true;
    return {static_cast<std::uint32_t>(std::distance(materials_.begin(), slot)),
            slot->generation};
}

void MaterialManager::setShader(MaterialHandle handle,
                                const std::filesystem::path& shaderPath) {
    Material* material = find(handle);
    if (!material) {
        Log::error("MaterialManager", "Cannot set Shader on an invalid Material");
        return;
    }
    const std::shared_ptr<ShaderAsset> asset =
        ASSET_MANAGER.loadShaderAsset(shaderPath);
    if (!asset) {
        Log::error("MaterialManager", "ShaderAsset failed to load: %s",
                   shaderPath.string().c_str());
        return;
    }
    material->setShader(std::make_shared<Shader>(*asset));
}

void MaterialManager::clear() {
    for (Slot& slot : materials_) {
        slot.material = {};
        slot.alive = false;
        ++slot.generation;
    }
}

void MaterialManager::destroy(MaterialHandle handle) {
    if (handle.index >= materials_.size()) {
        return;
    }
    Slot& slot = materials_[handle.index];
    if (!slot.alive || slot.generation != handle.generation) {
        return;
    }
    slot.material = {};
    slot.alive = false;
    ++slot.generation;
}

Material& MaterialManager::resolve(MaterialHandle handle) {
    return const_cast<Material&>(std::as_const(*this).resolve(handle));
}

Material* MaterialManager::find(MaterialHandle handle) {
    return const_cast<Material*>(std::as_const(*this).find(handle));
}

const Material* MaterialManager::find(MaterialHandle handle) const {
    if (handle.index >= materials_.size()) {
        return nullptr;
    }
    const Slot& slot = materials_[handle.index];
    return slot.alive && slot.generation == handle.generation
               ? &slot.material
               : nullptr;
}

const Material& MaterialManager::resolve(MaterialHandle handle) const {
    const Material* material = find(handle);
    if (!material) Log::fatal("MaterialManager", "Invalid or stale material handle");
    return *material;
}

} // namespace engine
