#pragma once

#include "core/base/InstanceManager.h"
#include "core/base/Singleton.h"
#include "render/mesh/Mesh.h"

namespace engine {

class MeshManager final : public Singleton<MeshManager>, public InstanceManager<Mesh, MeshHandle> {
public:
    [[nodiscard]] MeshHandle load(const VirtualPath& meshPath) override;
    [[nodiscard]] bool replace(MeshHandle handle, Mesh mesh);
    [[nodiscard]] bool replace(const VirtualPath& meshPath);

private:
    friend class Singleton<MeshManager>;
    MeshManager() = default;

    [[nodiscard]] const VirtualPath& pathOf(const Mesh& mesh) const override {
        return mesh.assetPath();
    }
    [[nodiscard]] bool validate(const Mesh& mesh) const override;
};

} // namespace engine

#define MESH_MANAGER (::engine::MeshManager::instance())
