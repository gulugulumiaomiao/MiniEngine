#pragma once

#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/mesh/Mesh.h"

namespace engine {

class MeshManager final
    : public Singleton<MeshManager>,
      public KeyedHandleRegistry<Mesh, MeshHandle, VirtualPath, VirtualPathHash> {
public:
    [[nodiscard]] MeshHandle load(const VirtualPath& meshPath);
    [[nodiscard]] bool replace(MeshHandle handle, Mesh mesh);
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshManager>;
    MeshManager() = default;

    [[nodiscard]] VirtualPath keyOf(const Mesh& mesh) const override {
        return mesh.assetPath();
    }
    [[nodiscard]] bool validate(const Mesh& mesh) const override;
};

} // namespace engine

#define MESH_MANAGER (::engine::MeshManager::instance())
