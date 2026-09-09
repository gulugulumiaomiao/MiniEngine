#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/mesh/MeshGpuCache.h"
#include "render/mesh/Mesh.h"
#include "render/renderer/DrawList.h"

#include <memory>

namespace engine {

class MeshGpuFactory;

namespace rhi {
class IDevice;
}

class MeshGpuManager final : public Singleton<MeshGpuManager> {
public:
    ~MeshGpuManager();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] MeshDrawInfo resolve(MeshHandle handle);
    void invalidate(MeshHandle handle);
    void shutdown();
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    friend class Singleton<MeshGpuManager>;
    MeshGpuManager();

    rhi::IDevice* device_{};
    MeshGpuCache cache_;
    std::unique_ptr<MeshGpuFactory> factory_;
};

} // namespace engine

#define MESH_GPU_MANAGER (::engine::MeshGpuManager::instance())
