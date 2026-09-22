#include "render/gpu/mesh/MeshStorage.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuManagerUtils.h"
#include "render/gpu/mesh/MeshStorageFactory.h"
#include "render/mesh/MeshManager.h"
#include "rhi/api/Device.h"

#include <utility>

namespace engine {

MeshStorage::MeshStorage() = default;
MeshStorage::~MeshStorage() = default;

bool MeshStorage::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("MeshStorage", "Manager is already initialized");
        return false;
    }
    device_ = &device;
    factory_ = std::make_unique<MeshStorageFactory>(device);
    MESH_RESOURCE_MANAGER.setDestroyObserver([this](RID handle) { invalidate(handle); });
    return true;
}

MeshDrawInfo MeshStorage::resolve(Mesh& mesh) {
    if (!initialized() || !mesh.resourceId())
        return {};

    const RID handle = mesh.resourceId();
    const MeshStorageCacheKey key = MeshStorageCache::key(handle, mesh.version());
    if (const MeshStorageEntry* cached = cache_.find(key))
        return cached->drawInfo;

    MeshStorageEntry created;
    if (!factory_->create({mesh}, created))
        return {};
    // Drop any upload of an earlier version of this Mesh before the new one goes resident.
    releaseBySource(cache_, *factory_, *device_, MeshStorageCache::sourceKey(handle));
    auto stored = cache_.store(key, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    mesh.markClean();
    return stored.stored->drawInfo;
}

void MeshStorage::invalidate(RID handle) {
    if (!initialized())
        return;
    releaseBySource(cache_, *factory_, *device_, MeshStorageCache::sourceKey(handle));
}

void MeshStorage::shutdown() {
    MESH_RESOURCE_MANAGER.setDestroyObserver({});
    if (!initialized())
        return;
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
    factory_.reset();
    device_ = nullptr;
}

} // namespace engine
