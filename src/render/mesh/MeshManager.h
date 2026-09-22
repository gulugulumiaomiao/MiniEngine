#pragma once

#include "asset/base/AssetId.h"
#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/mesh/Mesh.h"

#include <cstddef>
#include <functional>
#include <utility>

namespace engine {

class MeshManager final : public Singleton<MeshManager>,
                          public KeyedHandleRegistry<Mesh, RID, AssetId> {
public:
    [[nodiscard]] RID load(const AssetId& assetId);
    [[nodiscard]] RID load(const VirtualPath& meshPath);
    [[nodiscard]] RID clone(RID source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& meshPath);

    [[nodiscard]] RID createRuntime(const MeshBuildRecipe& recipe);
    [[nodiscard]] bool rebuildRuntime(RID handle, const MeshBuildRecipe& recipe);
    [[nodiscard]] bool destroyRuntime(RID handle);

    [[nodiscard]] RID insert(Mesh mesh);
    [[nodiscard]] RID insertUnkeyed(Mesh mesh);

    using KeyedHandleRegistry<Mesh, RID, AssetId>::find;
    [[nodiscard]] Mesh* find(const VirtualPath& meshPath);
    [[nodiscard]] const Mesh* find(const VirtualPath& meshPath) const;

    using KeyedHandleRegistry<Mesh, RID, AssetId>::findHandle;
    [[nodiscard]] RID findHandle(const VirtualPath& meshPath) const;

    [[nodiscard]] bool destroy(RID handle);
    void setDestroyObserver(std::function<void(RID)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear() override;
    [[nodiscard]] std::size_t size() const { return KeyedHandleRegistry::size(); }

    [[nodiscard]] bool replace(RID handle, Mesh mesh);
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshManager>;
    MeshManager() = default;

    [[nodiscard]] AssetId keyOf(const Mesh& mesh) const override { return mesh.assetId(); }
    [[nodiscard]] bool validate(const Mesh& mesh) const override;
    [[nodiscard]] RID loadFromPath(const VirtualPath& path, const AssetId& assetId);

    std::function<void(RID)> destroyObserver_;
};

} // namespace engine

#define MESH_MANAGER (::engine::MeshManager::instance())
