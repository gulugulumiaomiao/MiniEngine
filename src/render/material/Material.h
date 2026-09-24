#pragma once

#include "asset/base/AssetId.h"
#include "core/base/Ref.h"
#include "render/base/RenderHandle.h"
#include "render/shader/Shader.h"
#include "render/texture/Sampler.h"
#include "render/texture/Texture.h"

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
class MaterialAsset;
class MaterialResourceManager;
class Texture;

class Material final : public RefCounted {
public:
    Material() = default;
    ~Material() override;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = delete;
    Material& operator=(Material&&) = delete;
    std::string name;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    std::unordered_map<std::string, std::string> textures;
    std::vector<std::string> keywords;
    int renderQueue{2000};

    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] AssetId assetId() const { return assetId_; }
    [[nodiscard]] bool isAssetBacked() const { return assetId_.valid(); }
    [[nodiscard]] const Shader& shader() const;
    [[nodiscard]] const Ref<Shader>& shaderRef() const { return shader_; }
    [[nodiscard]] RID shaderHandle() const { return shader_ ? shader_->resourceId() : RID{}; }
    [[nodiscard]] RID resourceId() const { return resourceId_; }
    // 写回专用：只暴露 override（nullopt = 沿用 shader 默认），生效值 renderQueue
    // 字段是派生状态，写回时不得显式化。
    [[nodiscard]] std::optional<int> renderQueueOverride() const { return renderQueueOverride_; }
    void setShader(Ref<Shader> shader);

    // Editor write path: an override re-derives the effective renderQueue, and
    // clearing it (nullopt) falls back to the current Shader's sub-shader
    // default, keeping the derived field and the dirty/version bookkeeping in
    // step with the other setters. Idempotent: an unchanged value does not bump
    // the version.
    void setRenderQueue(std::optional<int> queueOverride);
    // Toggles one shader keyword (affects variant selection). Only keywords the
    // current Shader declares are accepted; rebuildForShader drops undeclared
    // ones anyway. Idempotent: re-applying the current state is a no-op.
    void setKeywordEnabled(const std::string& keyword, bool enabled);

    // Creates a detached runtime copy. The clone is not asset-backed: changes to
    // the source asset will not affect it, and it will not be returned by
    // MaterialResourceManager::findHandle(assetId).
    [[nodiscard]] Ref<Material> clone() const;

    [[nodiscard]] float getFloat(std::string_view name) const;
    [[nodiscard]] math::Vec2 getVec2(std::string_view name) const;
    [[nodiscard]] math::Vec3 getVec3(std::string_view name) const;
    [[nodiscard]] math::Vec4 getVec4(std::string_view name) const;
    [[nodiscard]] bool getBool(std::string_view name) const;
    [[nodiscard]] const std::string& getTexture(std::string_view name) const;
    [[nodiscard]] Ref<Texture> resolveTexture(std::string_view name) const;
    [[nodiscard]] Ref<Sampler> resolveSampler(std::string_view name) const;

    void setFloat(std::string_view name, float value);
    void setVec2(std::string_view name, const math::Vec2& value);
    void setVec3(std::string_view name, const math::Vec3& value);
    void setVec4(std::string_view name, const math::Vec4& value);
    void setBool(std::string_view name, bool value);
    void setTexture(std::string_view name, std::string value);
    void setTexture(std::string_view name, Ref<Texture> texture);
    void setTexture(std::string_view name, Ref<Texture> texture, const Ref<Sampler>& sampler);

    [[nodiscard]] std::span<const std::byte> uniformBytes() const { return uniformData; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    void markClean() { dirty_ = false; }

private:
    friend class MaterialResourceManager;
    friend class MaterialAsset;
    void initialize(AssetId assetId,
                    VirtualPath assetPath,
                    std::string materialName,
                    Ref<Shader> shader,
                    std::optional<int> renderQueueOverride);
    void rebuildForShader(Ref<Shader> shader, bool preserveValues);
    void rebuildFromAsset(const MaterialAsset& asset, Ref<Shader> newShader);
    [[nodiscard]] ShaderValue propertyValue(const ShaderPropertyDesc& property) const;
    void setPropertyValue(std::string_view name, const ShaderValue& value);
    void markChanged();

    AssetId assetId_;
    VirtualPath assetPath_;
    Ref<Shader> shader_;
    std::uint64_t shaderRevision_{};
    std::optional<int> renderQueueOverride_;
    mutable std::unordered_map<std::string, Ref<Texture>> textureRefs_;
    mutable std::unordered_map<std::string, Ref<Sampler>> textureSamplers_;
    bool suppressChanges_{};
    bool dirty_{true};
    std::uint64_t version_{1};
    RID resourceId_;
};

} // namespace engine
