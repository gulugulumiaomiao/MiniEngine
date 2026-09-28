#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/material/MaterialStorageCache.h"
#include "render/gpu/pipeline/GraphicsPipelineStorageCache.h"
#include "render/gpu/shader/ShaderStorageCache.h"

#include <type_traits>

namespace {

struct Request {};
struct Resource {};

static_assert(std::is_abstract_v<engine::IGpuResourceFactory<Request, Resource>>);
static_assert(std::is_abstract_v<engine::IGpuCache<int, Resource>>);
static_assert(
    std::is_base_of_v<
        engine::IGpuCache<engine::GraphicsPipelineStorageCacheKey, engine::GraphicsPipelineStorageEntry>,
        engine::GraphicsPipelineStorageCache>);

} // namespace

int main() {
    using namespace engine;

    GraphicsPipelineStorageCache simpleCache;
    ShaderStorageCache shaders;
    GraphicsPipelineStorageCache pipelines;
    MaterialStorageCache materials;
    if (!materials.initialize(2, 4))
        return 1;

    const GraphicsPipelineStorageCacheKey key = 7;
    if (simpleCache.find(key) || simpleCache.size() != 0)
        return 2;
    if (simpleCache.put(key, GraphicsPipelineStorageEntry{}) || !simpleCache.find(key)) {
        return 3;
    }
    const auto extracted = simpleCache.extractAll();
    if (extracted.size() != 1 || simpleCache.size() != 0)
        return 4;

    // Resident materials: slots persist across frames instead of being cleared.
    materials.beginFrame(0);
    const MaterialStorageCacheSlot first = materials.acquire(42);
    const MaterialStorageCacheSlot second = materials.acquire(42);
    if (!first.resource || first.cacheHit || second.resource != first.resource || !second.cacheHit)
        return 5;
    materials.beginFrame(1);
    const MaterialStorageCacheSlot otherFrame = materials.acquire(42);
    if (otherFrame.cacheHit || otherFrame.resource == first.resource)
        return 6; // each in-flight frame owns its own slot pool
    materials.beginFrame(0);
    const MaterialStorageCacheSlot backToFrame0 = materials.acquire(42);
    if (!backToFrame0.cacheHit || backToFrame0.resource != first.resource)
        return 7;
    if (materials.size() != 2)
        return 8; // one slot per in-flight frame

    // LRU eviction: fill the capacity, then force the oldest slot out. Slots may
    // reallocate while growing, so eviction is verified through ownership flags.
    materials.acquire(2);
    materials.acquire(3);
    materials.acquire(4); // frame 0 is now full (4 slots)
    const MaterialStorageCacheSlot evicted = materials.acquire(5);
    if (evicted.cacheHit || evicted.resource->ownerKey != 5 || !evicted.resource->pendingRelease)
        return 9; // material 42 was the least recently used, so its slot must be recycled
    if (materials.acquire(42).cacheHit)
        return 10; // the evicted lookup entry must be gone

    if (materials.extractAll().size() != 5 || materials.size() != 0)
        return 11;
    materials.reset();

    if (shaders.size() != 0 || pipelines.size() != 0)
        return 12;
    return 0;
}
