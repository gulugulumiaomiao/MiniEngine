#pragma once

#include "asset/base/AssetId.h"
#include "core/base/HandlePool.h"
#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/material/Material.h"

#include <functional>
#include <unordered_map>
#include <utility>

namespace engine {

class MaterialResourceManager final : public Singleton<MaterialResourceManager> {
public:
    [[nodiscard]] Ref<Material> load(const AssetId& assetId);
    [[nodiscard]] Ref<Material> load(const VirtualPath& materialAssetPath);
    [[nodiscard]] Ref<Material> errorMaterial();
    [[nodiscard]] Ref<Material> clone(const Ref<Material>& source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& materialPath);
    void setShader(const Ref<Material>& material, const VirtualPath& shaderPath);
    void refreshShader(const AssetId& shaderAssetId);

    [[nodiscard]] Ref<Material> insert(const Ref<Material>& material);
    [[nodiscard]] Ref<Material> insertUnkeyed(const Ref<Material>& material);
    [[nodiscard]] Ref<Material> find(RID handle) const;
    [[nodiscard]] Ref<Material> find(const AssetId& assetId) const;
    [[nodiscard]] Ref<Material> find(const VirtualPath& path) const;

    void setDestroyObserver(std::function<void(RID)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear();
    [[nodiscard]] std::size_t size() const { return resources_.size(); }

private:
    friend class Singleton<MaterialResourceManager>;
    friend class Material;
    MaterialResourceManager() = default;

    [[nodiscard]] Ref<Material> loadFromPath(const VirtualPath& materialPath,
                                             const AssetId& assetId);
    [[nodiscard]] Material* findRaw(RID handle) const;
    void unregister(Material* material);

    mutable HandlePool<Material*, RID> resources_;
    std::unordered_map<AssetId, RID> assetIndex_;
    std::function<void(RID)> destroyObserver_;
    Ref<Material> errorMaterial_;
};

using MaterialManager = MaterialResourceManager;

} // namespace engine

#define MATERIAL_RESOURCE_MANAGER (::engine::MaterialResourceManager::instance())
#define MATERIAL_MANAGER MATERIAL_RESOURCE_MANAGER
