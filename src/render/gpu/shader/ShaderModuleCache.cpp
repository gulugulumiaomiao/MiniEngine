#include "render/gpu/shader/ShaderModuleCache.h"

#include <utility>

namespace engine {

const ShaderModuleGpuResource* ShaderModuleCache::find(const CompiledShaderId& key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

std::optional<ShaderModuleGpuResource> ShaderModuleCache::put(CompiledShaderId key,
                                                              ShaderModuleGpuResource resource) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        entries_.emplace(key, std::move(resource));
        return std::nullopt;
    }
    ShaderModuleGpuResource replaced = std::move(found->second);
    found->second = std::move(resource);
    return replaced;
}

std::optional<ShaderModuleGpuResource> ShaderModuleCache::remove(const CompiledShaderId& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end())
        return std::nullopt;
    ShaderModuleGpuResource resource = std::move(found->second);
    entries_.erase(found);
    return resource;
}

std::vector<ShaderModuleCache::Entry> ShaderModuleCache::extractIf(const Predicate& predicate) {
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

std::vector<ShaderModuleCache::Entry> ShaderModuleCache::extractAll() {
    return extractIf([](CompiledShaderId, const ShaderModuleGpuResource&) { return true; });
}

} // namespace engine
