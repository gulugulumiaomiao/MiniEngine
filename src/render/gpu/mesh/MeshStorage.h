#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/mesh/MeshStorageCache.h"
#include "render/mesh/Mesh.h"
#include "render/renderer/DrawList.h"

#include <memory>

namespace engine {

class MeshStorageFactory;

namespace rhi {
class IDevice;
}

class MeshStorage final : public Singleton<MeshStorage> {
public:
    ~MeshStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] MeshDrawInfo resolve(Mesh& mesh);
    void invalidate(RID handle);
    void shutdown();
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    friend class Singleton<MeshStorage>;
    MeshStorage();

    rhi::IDevice* device_{};
    MeshStorageCache cache_;
    std::unique_ptr<MeshStorageFactory> factory_;
};

} // namespace engine

#define MESH_STORAGE (::engine::MeshStorage::instance())
