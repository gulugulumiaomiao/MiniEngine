#pragma once

#include "asset/base/AssetId.h"
#include "core/base/HandlePool.h"
#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/shader/Shader.h"

#include <functional>
#include <unordered_map>
#include <utility>

namespace engine {

class ShaderResourceManager final : public Singleton<ShaderResourceManager> {
public:
    [[nodiscard]] Ref<Shader> load(const AssetId& assetId);
    [[nodiscard]] Ref<Shader> load(const VirtualPath& shaderPath);
    [[nodiscard]] Ref<Shader> builtinColor();
    [[nodiscard]] Ref<Shader> clone(const Ref<Shader>& source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& shaderPath);

    [[nodiscard]] Ref<Shader> insert(const Ref<Shader>& shader);
    [[nodiscard]] Ref<Shader> insertUnkeyed(const Ref<Shader>& shader);
    [[nodiscard]] Ref<Shader> find(RID handle) const;
    [[nodiscard]] Ref<Shader> find(const AssetId& assetId) const;
    [[nodiscard]] Ref<Shader> find(const VirtualPath& shaderPath) const;

    [[nodiscard]] bool replace(const VirtualPath& shaderPath);
    void setDestroyObserver(std::function<void(RID)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear();
    [[nodiscard]] std::size_t size() const { return resources_.size(); }

private:
    friend class Singleton<ShaderResourceManager>;
    friend class Shader;
    ShaderResourceManager() = default;

    [[nodiscard]] Ref<Shader> loadFromPath(const VirtualPath& path, const AssetId& assetId);
    [[nodiscard]] Shader* findRaw(RID handle) const;
    void unregister(Shader* shader);

    mutable HandlePool<Shader*, RID> resources_;
    std::unordered_map<AssetId, RID> assetIndex_;
    std::function<void(RID)> destroyObserver_;
    Ref<Shader> builtinColor_;
};

using ShaderManager = ShaderResourceManager;

} // namespace engine

#define SHADER_RESOURCE_MANAGER (::engine::ShaderResourceManager::instance())
#define SHADER_MANAGER SHADER_RESOURCE_MANAGER
