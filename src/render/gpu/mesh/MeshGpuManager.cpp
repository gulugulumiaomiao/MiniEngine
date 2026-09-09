#include "render/gpu/mesh/MeshGpuManager.h"

#include "core/logging/Log.h"
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
    auto replaced = cache_.extractIf([source = MeshGpuCache::sourceKey(handle)](
                                        const MeshGpuCacheKey& candidate, const MeshGpuResource&) {
        return candidate.source == source;
    });
    if (!replaced.empty())
        device_->waitIdle();
    for (auto& [unused, resource] : replaced) {
        (void)unused;
        factory_->release(resource);
    }
    if (auto duplicate = cache_.put(key, std::move(created)))
        factory_->release(*duplicate);
    mesh->markClean();
    const MeshGpuResource* stored = cache_.find(key);
    return stored ? stored->drawInfo : MeshDrawInfo{};
}

void MeshGpuManager::invalidate(MeshHandle handle) {
    if (!initialized())
        return;
    auto removed =
        cache_.extractIf([source = MeshGpuCache::sourceKey(handle)](
                             const MeshGpuCacheKey& candidate, const MeshGpuResource&) {
            return candidate.source == source;
        });
    if (!removed.empty())
        device_->waitIdle();
    for (auto& [unused, resource] : removed) {
        (void)unused;
        factory_->release(resource);
    }
}

void MeshGpuManager::shutdown() {
    if (!initialized())
        return;
    for (auto& [unused, resource] : cache_.extractAll()) {
        (void)unused;
        factory_->release(resource);
    }
    factory_.reset();
    device_ = nullptr;
}

} // namespace engine
