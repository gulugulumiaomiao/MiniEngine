#include "render/gpu/pipeline/GraphicsPipelineCache.h"

#include <utility>

namespace engine {

const GraphicsPipelineGpuResource*
GraphicsPipelineCache::find(const GraphicsPipelineCacheKey& key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

std::optional<GraphicsPipelineGpuResource>
GraphicsPipelineCache::put(GraphicsPipelineCacheKey key, GraphicsPipelineGpuResource resource) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        entries_.emplace(key, std::move(resource));
        return std::nullopt;
    }
    GraphicsPipelineGpuResource replaced = std::move(found->second);
    found->second = std::move(resource);
    return replaced;
}

std::optional<GraphicsPipelineGpuResource>
GraphicsPipelineCache::remove(const GraphicsPipelineCacheKey& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end())
        return std::nullopt;
    GraphicsPipelineGpuResource resource = std::move(found->second);
    entries_.erase(found);
    return resource;
}

std::vector<GraphicsPipelineCache::Entry>
GraphicsPipelineCache::extractIf(const Predicate& predicate) {
    std::vector<Entry> result;
    for (auto iterator = entries_.begin(); iterator != entries_.end();) {
        if (!predicate(iterator->first, iterator->second)) {
            ++iterator;
            continue;
        }
        result.emplace_back(iterator->first, std::move(iterator->second));
        iterator = entries_.erase(iterator);
    }
    return result;
}

std::vector<GraphicsPipelineCache::Entry> GraphicsPipelineCache::extractAll() {
    return extractIf(
        [](GraphicsPipelineCacheKey, const GraphicsPipelineGpuResource&) { return true; });
}

} // namespace engine
