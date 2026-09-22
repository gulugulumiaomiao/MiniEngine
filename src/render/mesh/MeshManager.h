#pragma once

#include "asset/base/AssetId.h"
#include "core/base/HandlePool.h"
#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/mesh/Mesh.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace engine {

class MeshResourceManager final : public Singleton<MeshResourceManager> {
public:
    [[nodiscard]] Ref<Mesh> load(const AssetId& assetId);
    [[nodiscard]] Ref<Mesh> load(const VirtualPath& meshPath);
    [[nodiscard]] Ref<Mesh> clone(const Ref<Mesh>& source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& meshPath);

    [[nodiscard]] Ref<Mesh> createRuntime(const MeshBuildRecipe& recipe);
    [[nodiscard]] bool rebuildRuntime(const Ref<Mesh>& mesh, const MeshBuildRecipe& recipe);

    [[nodiscard]] Ref<Mesh> insert(const Ref<Mesh>& mesh);
    [[nodiscard]] Ref<Mesh> insertUnkeyed(const Ref<Mesh>& mesh);
    [[nodiscard]] Ref<Mesh> find(RID handle) const;
    [[nodiscard]] Ref<Mesh> find(const AssetId& assetId) const;
    [[nodiscard]] Ref<Mesh> find(const VirtualPath& meshPath) const;

    void setDestroyObserver(std::function<void(RID)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear();
    [[nodiscard]] std::size_t size() const { return resources_.size(); }
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshResourceManager>;
    friend class Mesh;
    MeshResourceManager() = default;

    [[nodiscard]] Ref<Mesh> loadFromPath(const VirtualPath& path, const AssetId& assetId);
    [[nodiscard]] Mesh* findRaw(RID handle) const;
    void unregister(Mesh* mesh);

    mutable HandlePool<Mesh*, RID> resources_;
    std::unordered_map<AssetId, RID> assetIndex_;
    std::function<void(RID)> destroyObserver_;
};

using MeshManager = MeshResourceManager;

} // namespace engine

#define MESH_RESOURCE_MANAGER (::engine::MeshResourceManager::instance())
#define MESH_MANAGER MESH_RESOURCE_MANAGER
