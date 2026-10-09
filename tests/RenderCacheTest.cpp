#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/GraphicsPipelineStorageCache.h"

#include <type_traits>

namespace {

struct Request {};
struct Resource {};

static_assert(std::is_abstract_v<engine::IGpuResourceFactory<Request, Resource>>);
static_assert(std::is_abstract_v<engine::IGpuCache<int, Resource>>);
static_assert(std::is_base_of_v<engine::IGpuCache<engine::GraphicsPipelineStorageCacheKey,
                                                  engine::GraphicsPipelineStorageEntry>,
                                engine::GraphicsPipelineStorageCache>);

} // namespace

int main() {
    using namespace engine;

    GraphicsPipelineStorageCache simpleCache;
    GraphicsPipelineStorageCache pipelines;

    const GraphicsPipelineStorageCacheKey key = 7;
    if (simpleCache.find(key) || simpleCache.size() != 0)
        return 2;
    if (simpleCache.put(key, GraphicsPipelineStorageEntry{}) || !simpleCache.find(key)) {
        return 3;
    }
    const auto extracted = simpleCache.extractAll();
    if (extracted.size() != 1 || simpleCache.size() != 0)
        return 4;

    if (pipelines.size() != 0)
        return 12;
    return 0;
}
