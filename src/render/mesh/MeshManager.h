#pragma once

#include "core/base/HandlePool.h"
#include "core/base/Singleton.h"
#include "render/mesh/Mesh.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace engine {

class MeshManager final : public Singleton<MeshManager> {
public:
    [[nodiscard]] MeshHandle load(const VirtualPath& meshPath);
    [[nodiscard]] MeshHandle createRuntime(const MeshBuildRecipe& recipe);
    [[nodiscard]] bool rebuildRuntime(MeshHandle handle, const MeshBuildRecipe& recipe);
    [[nodiscard]] bool destroyRuntime(MeshHandle handle);

    [[nodiscard]] MeshHandle insert(Mesh mesh);
    [[nodiscard]] Mesh* find(MeshHandle handle) { return meshes_.find(handle); }
    [[nodiscard]] const Mesh* find(MeshHandle handle) const { return meshes_.find(handle); }
    [[nodiscard]] Mesh* find(const VirtualPath& meshPath);
    [[nodiscard]] const Mesh* find(const VirtualPath& meshPath) const;
    [[nodiscard]] MeshHandle findHandle(const VirtualPath& meshPath) const;
    [[nodiscard]] bool destroy(MeshHandle handle);
    void setDestroyObserver(std::function<void(MeshHandle)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear();
    [[nodiscard]] std::size_t size() const { return meshes_.size(); }

    [[nodiscard]] bool replace(MeshHandle handle, Mesh mesh);
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshManager>;
    MeshManager() = default;

    [[nodiscard]] static bool validate(const Mesh& mesh);

    HandlePool<Mesh, MeshHandle> meshes_;
    std::unordered_map<VirtualPath, MeshHandle, VirtualPathHash> assetIndex_;
    std::function<void(MeshHandle)> destroyObserver_;
};

} // namespace engine

#define MESH_MANAGER (::engine::MeshManager::instance())
