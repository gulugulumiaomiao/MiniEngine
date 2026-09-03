#pragma once

#include "renderer/RenderResources.h"
#include "renderer/Shader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class AssetManager;

class Material final {
public:
    std::string name;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    std::unordered_map<std::string, std::string> textures;
    std::vector<std::string> keywords;
    int renderQueue{2000};

    [[nodiscard]] const Shader& shader() const;
    [[nodiscard]] const std::shared_ptr<Shader>& shaderReference() const {
        return shader_;
    }
    void setShader(std::shared_ptr<Shader> shader);

    [[nodiscard]] float getFloat(std::string_view name) const;
    [[nodiscard]] math::Vec2 getVec2(std::string_view name) const;
    [[nodiscard]] math::Vec3 getVec3(std::string_view name) const;
    [[nodiscard]] math::Vec4 getVec4(std::string_view name) const;
    [[nodiscard]] bool getBool(std::string_view name) const;
    [[nodiscard]] const std::string& getTexture(std::string_view name) const;

    void setFloat(std::string_view name, float value);
    void setVec2(std::string_view name, const math::Vec2& value);
    void setVec3(std::string_view name, const math::Vec3& value);
    void setVec4(std::string_view name, const math::Vec4& value);
    void setBool(std::string_view name, bool value);
    void setTexture(std::string_view name, std::string value);

    [[nodiscard]] std::span<const std::byte> uniformBytes() const { return uniformData; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    void markClean() { dirty_ = false; }

private:
    friend class MaterialManager;
    void initialize(std::string materialName, std::shared_ptr<Shader> shader,
                    std::optional<int> renderQueueOverride);
    void rebuildForShader(std::shared_ptr<Shader> shader, bool preserveValues);
    [[nodiscard]] ShaderValue propertyValue(const ShaderPropertyDesc& property) const;
    void setPropertyValue(std::string_view name, const ShaderValue& value);
    void markChanged();

    std::shared_ptr<Shader> shader_;
    std::optional<int> renderQueueOverride_;
    bool suppressChanges_{};
    bool dirty_{true};
    std::uint64_t version_{1};
};

class MaterialManager final {
public:
    [[nodiscard]] static MaterialManager& instance();

    MaterialManager(const MaterialManager&) = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;

    [[nodiscard]] MaterialHandle createInstance(
        const std::filesystem::path& materialAssetPath);
    [[nodiscard]] MaterialHandle createInstance(
        std::shared_ptr<MaterialAsset> materialAsset);
    void destroy(MaterialHandle handle);
    void setShader(MaterialHandle handle, const std::filesystem::path& shaderPath);
    [[nodiscard]] Material* find(MaterialHandle handle);
    [[nodiscard]] const Material* find(MaterialHandle handle) const;
    [[nodiscard]] Material& resolve(MaterialHandle handle);
    [[nodiscard]] const Material& resolve(MaterialHandle handle) const;
    void clear();

private:
    MaterialManager() = default;

    struct Slot {
        Material material;
        std::uint32_t generation{1};
        bool alive{};
    };

    std::vector<Slot> materials_;
};

} // namespace engine

#define MATERIAL_MANAGER (::engine::MaterialManager::instance())
