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
    if (!materials.initialize(2))
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

    materials.beginFrame(0);
    const MaterialBindingCacheSlot first = materials.acquire(42);
    const MaterialBindingCacheSlot second = materials.acquire(42);
    if (!first.resource || first.cacheHit || second.resource != first.resource || !second.cacheHit)
        return 5;
    if (materials.extractAll().size() != 1 || materials.size() != 0)
        return 6;
    materials.reset();

    if (textures.size() != 0 || shaders.size() != 0 || pipelines.size() != 0)
        return 7;
    return 0;
}
