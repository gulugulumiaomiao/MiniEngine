#pragma once

#include "render/renderer/RenderResources.h"
#include "render/shader/Shader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class AssetManager;
class Material;
class MaterialManager;

class MaterialAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Material; }

    std::string name;
    VirtualPath shader;
    std::unordered_map<std::string, ShaderValue> properties;
    std::vector<std::string> keywords;
    std::optional<int> renderQueue;

    [[nodiscard]] Material instantiate(ShaderHandle shaderHandle) const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

namespace detail {

[[nodiscard]] std::shared_ptr<MaterialAsset> parseMaterialAsset(const VirtualPath& path,
                                                                std::string_view source);

} // namespace detail

[[nodiscard]] bool validateMaterialAsset(const MaterialAsset& material,
                                         const ShaderAsset& shader,
                                         const VirtualPath& materialPath);

class Material final {
public:
    std::string name;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    std::unordered_map<std::string, std::string> textures;
    std::vector<std::string> keywords;
    int renderQueue{2000};

    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] const Shader& shader() const;
    [[nodiscard]] ShaderHandle shaderHandle() const { return shaderHandle_; }
    void setShader(ShaderHandle shader);

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
    friend class MaterialAsset;
    void initialize(VirtualPath assetPath,
                    std::string materialName,
                    ShaderHandle shader,
                    std::optional<int> renderQueueOverride);
    void rebuildForShader(ShaderHandle shader, bool preserveValues);
    [[nodiscard]] ShaderValue propertyValue(const ShaderPropertyDesc& property) const;
    void setPropertyValue(std::string_view name, const ShaderValue& value);
    void markChanged();

    VirtualPath assetPath_;
    ShaderHandle shaderHandle_;
    std::uint64_t shaderRevision_{};
    std::optional<int> renderQueueOverride_;
    bool suppressChanges_{};
    bool dirty_{true};
    std::uint64_t version_{1};
};

} // namespace engine
