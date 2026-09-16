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
                          public KeyedHandleRegistry<Mesh, MeshHandle, AssetId> {
public:
    [[nodiscard]] MeshHandle load(const AssetId& assetId);
    [[nodiscard]] MeshHandle load(const VirtualPath& meshPath);
    [[nodiscard]] MeshHandle clone(MeshHandle source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& meshPath);

    [[nodiscard]] MeshHandle createRuntime(const MeshBuildRecipe& recipe);
    [[nodiscard]] bool rebuildRuntime(MeshHandle handle, const MeshBuildRecipe& recipe);
    [[nodiscard]] bool destroyRuntime(MeshHandle handle);

    [[nodiscard]] MeshHandle insert(Mesh mesh);
    [[nodiscard]] MeshHandle insertUnkeyed(Mesh mesh);

    using KeyedHandleRegistry<Mesh, MeshHandle, AssetId>::find;
    [[nodiscard]] Mesh* find(const VirtualPath& meshPath);
    [[nodiscard]] const Mesh* find(const VirtualPath& meshPath) const;

    using KeyedHandleRegistry<Mesh, MeshHandle, AssetId>::findHandle;
    [[nodiscard]] MeshHandle findHandle(const VirtualPath& meshPath) const;

    [[nodiscard]] bool destroy(MeshHandle handle);
    void setDestroyObserver(std::function<void(MeshHandle)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear() override;
    [[nodiscard]] std::size_t size() const { return KeyedHandleRegistry::size(); }

    [[nodiscard]] bool replace(MeshHandle handle, Mesh mesh);
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshManager>;
    MeshManager() = default;

    [[nodiscard]] AssetId keyOf(const Mesh& mesh) const override { return mesh.assetId(); }
    [[nodiscard]] bool validate(const Mesh& mesh) const override;
    [[nodiscard]] MeshHandle loadFromPath(const VirtualPath& path, const AssetId& assetId);

    std::function<void(MeshHandle)> destroyObserver_;
};

} // namespace engine

#define MESH_MANAGER (::engine::MeshManager::instance())
