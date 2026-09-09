#include "render/gpu/mesh/MeshGpuCache.h"

#include "core/hash.h"

#include <utility>

namespace engine {

std::size_t MeshGpuCacheKeyHash::operator()(const MeshGpuCacheKey& key) const {
    Hash64 hash = kFnv1a64OffsetBasis;
    hashAppend(hash, key.source);
    hashAppend(hash, key.version);
    return static_cast<std::size_t>(hash);
}

std::uint64_t MeshGpuCache::sourceKey(MeshHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

MeshGpuCacheKey MeshGpuCache::key(MeshHandle handle, std::uint64_t version) {
    return {sourceKey(handle), version};
}

const MeshGpuResource* MeshGpuCache::find(const MeshGpuCacheKey& key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

std::optional<MeshGpuResource> MeshGpuCache::put(MeshGpuCacheKey key, MeshGpuResource resource) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        entries_.emplace(key, std::move(resource));
        return std::nullopt;
    }
    MeshGpuResource replaced = std::move(found->second);
    found->second = std::move(resource);
    return replaced;
}

std::optional<MeshGpuResource> MeshGpuCache::remove(const MeshGpuCacheKey& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end())
        return std::nullopt;
    MeshGpuResource resource = std::move(found->second);
    entries_.erase(found);
    return resource;
}

std::vector<MeshGpuCache::Entry> MeshGpuCache::extractIf(const Predicate& predicate) {
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

std::vector<MeshGpuCache::Entry> MeshGpuCache::extractAll() {
    return extractIf([](const MeshGpuCacheKey&, const MeshGpuResource&) { return true; });
}

} // namespace engine
