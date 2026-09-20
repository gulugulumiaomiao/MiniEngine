#include "render/global_uniform/GlobalUniformManager.h"

#include "core/logging/Log.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace engine {

namespace {

const char* glslTypeName(ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range: return "float";
    case ShaderPropertyType::Boolean: return "bool";
    case ShaderPropertyType::Vec2: return "vec2";
    case ShaderPropertyType::Vec3: return "vec3";
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color: return "vec4";
    case ShaderPropertyType::Matrix: return "mat4";
    case ShaderPropertyType::Texture2D: return "sampler2D";
    }
    return "unknown";
}

bool compatibleGlobalTypes(ShaderPropertyType left, ShaderPropertyType right) {
    if (left == right)
        return true;
    if ((left == ShaderPropertyType::Float || left == ShaderPropertyType::Range) &&
        (right == ShaderPropertyType::Float || right == ShaderPropertyType::Range))
        return true;
    if ((left == ShaderPropertyType::Vec4 || left == ShaderPropertyType::Color) &&
        (right == ShaderPropertyType::Vec4 || right == ShaderPropertyType::Color))
        return true;
    if (left == ShaderPropertyType::Matrix && right == ShaderPropertyType::Matrix)
        return true;
    return false;
}

template <typename T>
void writeValueBytes(std::vector<std::byte>& data, const UniformMemberLayout& member, const T& value) {
    std::memcpy(data.data() + member.offset, &value, sizeof(T));
}

} // namespace

GlobalUniformManager::GlobalUniformManager() = default;
GlobalUniformManager::~GlobalUniformManager() = default;

void GlobalUniformManager::registerGlobalProperties(std::span<const ShaderPropertyDesc> properties) {
    bool changed = false;
    for (const ShaderPropertyDesc& property : properties) {
        const auto existing = globals_.find(property.name);
        if (existing == globals_.end()) {
            globals_.emplace(property.name, KnownGlobal{property.type, property.defaultValue});
            changed = true;
            continue;
        }
        if (!compatibleGlobalTypes(existing->second.type, property.type)) {
            Log::error("GlobalUniform",
                       "Global property '%s' was registered as %s and later as %s",
                       property.name.c_str(),
                       glslTypeName(existing->second.type),
                       glslTypeName(property.type));
            continue;
        }
        // Keep the current value; only normalize the stored type to the latest declaration
        // so that setters and GLSL generation agree.
        existing->second.type = property.type;
    }
    if (changed)
        rebuild();
}

void GlobalUniformManager::clear() {
    globals_.clear();
    textureProperties_.clear();
    layout_ = UniformBlockLayout{};
    uniformData_.clear();
    markChanged();
}

void GlobalUniformManager::setFloat(std::string_view name, float value) {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end()) {
        globals_.emplace(std::string{name}, KnownGlobal{ShaderPropertyType::Float, value});
        rebuild();
        return;
    }
    if (!compatibleGlobalTypes(found->second.type, ShaderPropertyType::Float)) {
        Log::error("GlobalUniform",
                   "Cannot set float on global property '%.*s' of type %s",
                   static_cast<int>(name.size()),
                   name.data(),
                   glslTypeName(found->second.type));
        return;
    }
    found->second.value = value;
    rebuildBuffer();
}

void GlobalUniformManager::setVector(std::string_view name, const math::Vec4& value) {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end()) {
        globals_.emplace(std::string{name}, KnownGlobal{ShaderPropertyType::Vec4, value});
        rebuild();
        return;
    }
    if (!compatibleGlobalTypes(found->second.type, ShaderPropertyType::Vec4)) {
        Log::error("GlobalUniform",
                   "Cannot set vector on global property '%.*s' of type %s",
                   static_cast<int>(name.size()),
                   name.data(),
                   glslTypeName(found->second.type));
        return;
    }
    found->second.value = value;
    rebuildBuffer();
}

void GlobalUniformManager::setColor(std::string_view name, const math::Vec4& value) {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end()) {
        globals_.emplace(std::string{name}, KnownGlobal{ShaderPropertyType::Color, value});
        rebuild();
        return;
    }
    if (!compatibleGlobalTypes(found->second.type, ShaderPropertyType::Color)) {
        Log::error("GlobalUniform",
                   "Cannot set color on global property '%.*s' of type %s",
                   static_cast<int>(name.size()),
                   name.data(),
                   glslTypeName(found->second.type));
        return;
    }
    found->second.value = value;
    rebuildBuffer();
}

void GlobalUniformManager::setBool(std::string_view name, bool value) {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end()) {
        globals_.emplace(std::string{name}, KnownGlobal{ShaderPropertyType::Boolean, value});
        rebuild();
        return;
    }
    if (!compatibleGlobalTypes(found->second.type, ShaderPropertyType::Boolean)) {
        Log::error("GlobalUniform",
                   "Cannot set bool on global property '%.*s' of type %s",
                   static_cast<int>(name.size()),
                   name.data(),
                   glslTypeName(found->second.type));
        return;
    }
    found->second.value = value;
    rebuildBuffer();
}

void GlobalUniformManager::setMatrix(std::string_view name, const math::Mat44& value) {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end()) {
        globals_.emplace(std::string{name}, KnownGlobal{ShaderPropertyType::Matrix, value});
        rebuild();
        return;
    }
    if (!compatibleGlobalTypes(found->second.type, ShaderPropertyType::Matrix)) {
        Log::error("GlobalUniform",
                   "Cannot set matrix on global property '%.*s' of type %s",
                   static_cast<int>(name.size()),
                   name.data(),
                   glslTypeName(found->second.type));
        return;
    }
    found->second.value = value;
    rebuildBuffer();
}

void GlobalUniformManager::setTexture(std::string_view name, std::string_view texturePath) {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end()) {
        globals_.emplace(std::string{name},
                         KnownGlobal{ShaderPropertyType::Texture2D,
                                     std::string{texturePath}});
        rebuild();
        return;
    }
    if (found->second.type != ShaderPropertyType::Texture2D) {
        Log::error("GlobalUniform",
                   "Cannot set texture on global property '%.*s' of type %s",
                   static_cast<int>(name.size()),
                   name.data(),
                   glslTypeName(found->second.type));
        return;
    }
    found->second.value = std::string{texturePath};
    markChanged();
}

const std::string* GlobalUniformManager::findTexture(std::string_view name) const {
    const auto found = globals_.find(std::string{name});
    if (found == globals_.end() || found->second.type != ShaderPropertyType::Texture2D)
        return nullptr;
    return std::get_if<std::string>(&found->second.value);
}

void GlobalUniformManager::rebuild() {
    // Numeric globals share one std140 block; sort by name so the layout is stable
    // regardless of shader registration order.
    std::vector<ShaderPropertyDesc> numeric;
    textureProperties_.clear();
    for (const auto& [name, global] : globals_) {
        ShaderPropertyDesc desc;
        desc.name = name;
        desc.displayName = name;
        desc.type = global.type;
        desc.defaultValue = global.value;
        if (global.type == ShaderPropertyType::Texture2D) {
            textureProperties_.push_back(std::move(desc));
        } else {
            numeric.push_back(std::move(desc));
        }
    }
    std::ranges::sort(numeric, [](const ShaderPropertyDesc& left, const ShaderPropertyDesc& right) {
        return left.name < right.name;
    });
    std::ranges::sort(textureProperties_,
                      [](const ShaderPropertyDesc& left, const ShaderPropertyDesc& right) {
                          return left.name < right.name;
                      });

    layout_ = buildUniformBlockLayout(numeric);
    rebuildBuffer();
}

void GlobalUniformManager::rebuildBuffer() {
    uniformData_.assign(layout_.byteSize, std::byte{0});
    for (const UniformMemberLayout& member : layout_.members) {
        const auto found = globals_.find(member.name);
        if (found == globals_.end())
            continue;
        const KnownGlobal& global = found->second;
        switch (member.type) {
        case ShaderPropertyType::Float:
        case ShaderPropertyType::Range: {
            if (const float* value = std::get_if<float>(&global.value))
                writeValueBytes(uniformData_, member, *value);
            break;
        }
        case ShaderPropertyType::Boolean: {
            if (const bool* value = std::get_if<bool>(&global.value)) {
                const std::uint32_t encoded = *value ? 1U : 0U;
                writeValueBytes(uniformData_, member, encoded);
            }
            break;
        }
        case ShaderPropertyType::Vec2: {
            if (const math::Vec2* value = std::get_if<math::Vec2>(&global.value))
                writeValueBytes(uniformData_, member, *value);
            break;
        }
        case ShaderPropertyType::Vec3: {
            if (const math::Vec3* value = std::get_if<math::Vec3>(&global.value))
                writeValueBytes(uniformData_, member, *value);
            break;
        }
        case ShaderPropertyType::Vec4:
        case ShaderPropertyType::Color: {
            if (const math::Vec4* value = std::get_if<math::Vec4>(&global.value))
                writeValueBytes(uniformData_, member, *value);
            break;
        }
        case ShaderPropertyType::Matrix: {
            if (const math::Mat44* value = std::get_if<math::Mat44>(&global.value))
                writeValueBytes(uniformData_, member, *value);
            break;
        }
        case ShaderPropertyType::Texture2D:
            break;
        }
    }
    markChanged();
}

void GlobalUniformManager::markChanged() {
    if (version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::fatal("GlobalUniform", "Global uniform version overflow");
    }
    ++version_;
}

} // namespace engine
