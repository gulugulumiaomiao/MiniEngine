#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/material/MaterialBindingCache.h"
#include "render/gpu/mesh/MeshGpuCache.h"
#include "render/gpu/pipeline/GraphicsPipelineCache.h"
#include "render/gpu/shader/ShaderModuleCache.h"
#include "render/gpu/texture/TextureGpuCache.h"

#include <type_traits>

namespace {

struct Request {};
struct Resource {};

static_assert(std::is_abstract_v<engine::IGpuResourceFactory<Request, Resource>>);
static_assert(std::is_abstract_v<engine::IGpuCache<int, Resource>>);
static_assert(std::is_base_of_v<engine::IGpuCache<engine::MeshGpuCacheKey, engine::MeshGpuResource>,
                                engine::MeshGpuCache>);
static_assert(
    std::is_base_of_v<engine::IGpuCache<engine::TextureGpuCacheKey, engine::TextureGpuResource>,
                      engine::TextureGpuCache>);

} // namespace

int main() {
    using namespace engine;

    MeshGpuCache meshes;
    TextureGpuCache textures;
    ShaderModuleCache shaders;
    GraphicsPipelineCache pipelines;
    MaterialBindingCache materials;
    if (!materials.initialize(2, 4))
        return 1;

    const MeshGpuCacheKey key{7, 3};
    if (meshes.find(key) || meshes.size() != 0)
        return 2;
    if (meshes.put(key, MeshGpuResource{}) || !meshes.find(key)) {
        return 3;
    }
    const auto extracted = meshes.extractAll();
    if (extracted.size() != 1 || meshes.size() != 0)
        return 4;

    // Resident materials: slots persist across frames instead of being cleared.
    materials.beginFrame(0);
    const MaterialBindingCacheSlot first = materials.acquire(42);
    const MaterialBindingCacheSlot second = materials.acquire(42);
    if (!first.resource || first.cacheHit || second.resource != first.resource || !second.cacheHit)
        return 5;
    materials.beginFrame(1);
    const MaterialBindingCacheSlot otherFrame = materials.acquire(42);
    if (otherFrame.cacheHit || otherFrame.resource == first.resource)
        return 6; // each in-flight frame owns its own slot pool
    materials.beginFrame(0);
    const MaterialBindingCacheSlot backToFrame0 = materials.acquire(42);
    if (!backToFrame0.cacheHit || backToFrame0.resource != first.resource)
        return 7;
    if (materials.size() != 2)
        return 8; // one slot per in-flight frame

    // LRU eviction: fill the capacity, then force the oldest slot out. Slots may
    // reallocate while growing, so eviction is verified through ownership flags.
    materials.acquire(2);
    materials.acquire(3);
    materials.acquire(4); // frame 0 is now full (4 slots)
    const MaterialBindingCacheSlot evicted = materials.acquire(5);
    if (evicted.cacheHit || evicted.resource->ownerKey != 5 || !evicted.resource->pendingRelease)
        return 9; // material 42 was the least recently used, so its slot must be recycled
    if (materials.acquire(42).cacheHit)
        return 10; // the evicted lookup entry must be gone

    if (materials.extractAll().size() != 5 || materials.size() != 0)
        return 11;
    materials.reset();

    if (textures.size() != 0 || shaders.size() != 0 || pipelines.size() != 0)
        return 12;
    return 0;
}
