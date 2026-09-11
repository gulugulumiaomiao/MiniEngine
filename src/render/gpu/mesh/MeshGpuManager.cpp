#include "render/gpu/mesh/MeshGpuManager.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuManagerUtils.h"
#include "render/gpu/mesh/MeshGpuFactory.h"
#include "render/mesh/MeshManager.h"
#include "rhi/api/Device.h"

#include <utility>

namespace engine {

MeshGpuManager::MeshGpuManager() = default;
MeshGpuManager::~MeshGpuManager() = default;

bool MeshGpuManager::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("MeshGpuManager", "Manager is already initialized");
        return false;
    }
    device_ = &device;
    factory_ = std::make_unique<MeshGpuFactory>(device);
    MESH_MANAGER.setDestroyObserver([this](MeshHandle handle) { invalidate(handle); });
    return true;
}

MeshDrawInfo MeshGpuManager::resolve(MeshHandle handle) {
    Mesh* mesh = MESH_MANAGER.find(handle);
    if (!initialized() || !mesh)
        return {};

    const MeshGpuCacheKey key = MeshGpuCache::key(handle, mesh->version());
    if (const MeshGpuResource* cached = cache_.find(key))
        return cached->drawInfo;

    MeshGpuResource created;
    if (!factory_->create({*mesh}, created))
        return {};
    // Drop any upload of an earlier version of this Mesh before the new one goes resident.
    releaseBySource(cache_, *factory_, *device_, MeshGpuCache::sourceKey(handle));
    auto stored = cache_.store(key, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    mesh->markClean();
    return stored.stored->drawInfo;
}

void MeshGpuManager::invalidate(MeshHandle handle) {
    if (!initialized())
        return;
    releaseBySource(cache_, *factory_, *device_, MeshGpuCache::sourceKey(handle));
}

void MeshGpuManager::shutdown() {
    MESH_MANAGER.setDestroyObserver({});
    if (!initialized())
        return;
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
    factory_.reset();
    device_ = nullptr;
}

} // namespace engine
