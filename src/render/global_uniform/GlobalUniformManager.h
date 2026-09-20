#pragma once

#include "core/base/Singleton.h"
#include "render/shader/Shader.h"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// Shared CPU-side storage for shader global properties.
//
// Global properties are declared per-shader in shader.json's "globalProperties" array.
// As shaders are constructed they register their declarations here; the manager merges
// them into a single std140 uniform block layout and a matching byte buffer. Any shader
// that declares a matching name reads the same value, making this the equivalent of
// Unity's Shader.SetGlobalXXX family.
class GlobalUniformManager final : public Singleton<GlobalUniformManager> {
public:
    ~GlobalUniformManager();

    // Register global properties declared by a shader. Called automatically from
    // Shader construction. Rebuilds the shared uniform block layout if the known
    // set of globals changes.
    void registerGlobalProperties(std::span<const ShaderPropertyDesc> properties);

    // Clear all registered globals and reset the buffer. Exposed for tests so that
    // each case starts from a clean state; the engine does not call this at runtime.
    void clear();

    // Unity-style API. These are also exposed as static methods on Shader.
    void setFloat(std::string_view name, float value);
    void setVector(std::string_view name, const math::Vec4& value);
    void setColor(std::string_view name, const math::Vec4& value);
    void setBool(std::string_view name, bool value);
    void setMatrix(std::string_view name, const math::Mat44& value);
    void setTexture(std::string_view name, std::string_view texturePath);

    [[nodiscard]] const UniformBlockLayout& uniformBlockLayout() const { return layout_; }
    [[nodiscard]] std::span<const std::byte> uniformBytes() const { return uniformData_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }

    [[nodiscard]] const std::string* findTexture(std::string_view name) const;
    [[nodiscard]] std::span<const ShaderPropertyDesc> textureProperties() const {
        return textureProperties_;
    }

private:
    friend class Singleton<GlobalUniformManager>;
    GlobalUniformManager();

    struct KnownGlobal {
        ShaderPropertyType type;
        ShaderValue value;
    };

    void rebuild();
    void rebuildBuffer();
    void markChanged();

    std::unordered_map<std::string, KnownGlobal> globals_;
    std::vector<ShaderPropertyDesc> textureProperties_;
    UniformBlockLayout layout_;
    std::vector<std::byte> uniformData_;
    std::uint64_t version_{1};
};

} // namespace engine

#define GLOBAL_UNIFORM_MANAGER (::engine::GlobalUniformManager::instance())
