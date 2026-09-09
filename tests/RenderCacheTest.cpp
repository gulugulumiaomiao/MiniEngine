#include "render/gpu/GpuCacheRegistry.h"
#include "render/gpu/common/IGpuResourceFactory.h"

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

    if (!GPU_CACHE.initialize(2) || !GPU_CACHE.initialized())
        return 1;

    const MeshGpuCacheKey key{7, 3};
    if (GPU_CACHE.meshes().find(key) || GPU_CACHE.meshes().size() != 0)
        return 2;
    if (GPU_CACHE.meshes().put(key, MeshGpuResource{}) || !GPU_CACHE.meshes().find(key) ||
        GPU_CACHE.shutdown()) {
        return 3;
    }
    const auto extracted = GPU_CACHE.meshes().extractAll();
    if (extracted.size() != 1 || GPU_CACHE.meshes().size() != 0)
        return 4;
    if (!GPU_CACHE.shutdown() || GPU_CACHE.initialized())
        return 5;
    return 0;
}
