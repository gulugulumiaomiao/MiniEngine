#pragma once

#include "core/base/Ref.h"
#include "render/material/MaterialTypes.h"
#include "render/uniform/UniformSet.h"
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

class Material;
class Texture;

class Material final : public RefCounted {
public:
    explicit Material(MaterialCreateInfo info);
    static Ref<Material> errorMaterial;
    bool rebuild(MaterialCreateInfo info);
    void ensureShaderCurrent() const;
    std::uint64_t instanceId() const { return instanceId_; }
    int effectiveRenderQueue() const {
        ensureShaderCurrent();
        return renderQueue;
    }
    Ref<UniformSet> uniformSet(const ShaderProgram& program, std::uint32_t frameIndex) const;
    ~Material() override;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) = delete;
    Material& operator=(Material&&) = delete;
    std::string name;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    mutable std::unordered_map<std::string, MaterialTextureBinding> textures;
    std::vector<std::string> keywords;
    int renderQueue{2000};

    [[nodiscard]] const Shader& shader() const;
    [[nodiscard]] const Ref<Shader>& shaderRef() const { return shader_; }
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
    // Material owns enabled keywords independently of Shader declarations.
    // Each pass ignores undeclared keywords during program selection.
    // Idempotent: re-applying the current state is a no-op.
    void setKeywordEnabled(const std::string& keyword, bool enabled);
    void enableKeyword(const std::string& keyword) { setKeywordEnabled(keyword, true); }
    void disableKeyword(const std::string& keyword) { setKeywordEnabled(keyword, false); }
    [[nodiscard]] bool isKeywordEnabled(std::string_view keyword) const;
    [[nodiscard]] std::uint64_t variantKey(const ShaderPass& pass) const;
    [[nodiscard]] Ref<ShaderProgram> program(const ShaderPass& pass) const;

    [[nodiscard]] Ref<Material> clone() const;

    [[nodiscard]] float getFloat(std::string_view name) const;
    [[nodiscard]] math::Vec2 getVec2(std::string_view name) const;
    [[nodiscard]] math::Vec3 getVec3(std::string_view name) const;
    [[nodiscard]] math::Vec4 getVec4(std::string_view name) const;
    [[nodiscard]] math::Mat44 getMatrix(std::string_view name) const;
    [[nodiscard]] bool getBool(std::string_view name) const;
    [[nodiscard]] Ref<Texture> getTexture(std::string_view name) const;
    [[nodiscard]] Ref<Texture> resolveTexture(std::string_view name) const;
    [[nodiscard]] Ref<Sampler> resolveSampler(std::string_view name) const;

    void setFloat(std::string_view name, float value);
    void setVec2(std::string_view name, const math::Vec2& value);
    void setVec3(std::string_view name, const math::Vec3& value);
    void setVec4(std::string_view name, const math::Vec4& value);
    void setMatrix(std::string_view name, const math::Mat44& value);
    void setBool(std::string_view name, bool value);
    void setTexture(std::string_view name, MaterialTextureBinding binding);
    void setTexture(std::string_view name, Ref<Texture> texture);
    void setTexture(std::string_view name, Ref<Texture> texture, const Ref<Sampler>& sampler);

    [[nodiscard]] std::span<const std::byte> uniformBytes() const {
        ensureShaderCurrent();
        return uniformData;
    }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] std::uint64_t version() const {
        ensureShaderCurrent();
        return version_;
    }
    void markClean() { dirty_ = false; }

private:
    void rebuildForShader(Ref<Shader> shader, bool preserveValues);
    [[nodiscard]] ShaderValue propertyValue(const ShaderPropertyInfo& property) const;
    void setPropertyValue(std::string_view name, const ShaderValue& value);
    void markChanged();

    Ref<Shader> shader_;
    std::uint64_t shaderRevision_{};
    std::optional<int> renderQueueOverride_;
    std::function<MaterialTextureBinding(const ShaderPropertyInfo&)> defaultTextureBinding_;
    struct BindingCache {
        Ref<UniformSet> set;
        std::uint64_t version{};
        std::vector<rhi::TextureBinding> textures;
    };
    mutable std::unordered_map<std::uint32_t, std::unordered_map<ShaderProgramId, BindingCache>>
        bindings_;
    mutable std::uint64_t deviceUid_{};
    bool rebuilding_{};
    bool suppressChanges_{};
    bool dirty_{true};
    std::uint64_t version_{1};
    std::uint64_t instanceId_{};
};

} // namespace engine
