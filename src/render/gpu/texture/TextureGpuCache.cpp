#include "render/gpu/texture/TextureGpuCache.h"

#include "core/hash.h"

#include <utility>

namespace engine {

std::size_t TextureGpuCacheKeyHash::operator()(const TextureGpuCacheKey& key) const {
    Hash64 hash = kFnv1a64OffsetBasis;
    hashAppend(hash, key.source);
    hashAppend(hash, key.version);
    return static_cast<std::size_t>(hash);
}

std::uint64_t TextureGpuCache::sourceKey(TextureHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

TextureGpuCacheKey TextureGpuCache::key(TextureHandle handle, std::uint64_t version) {
    return {sourceKey(handle), version};
}

const TextureGpuResource* TextureGpuCache::find(const TextureGpuCacheKey& key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : &found->second;
}

std::optional<TextureGpuResource> TextureGpuCache::put(TextureGpuCacheKey key,
                                                       TextureGpuResource resource) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        entries_.emplace(key, std::move(resource));
        return std::nullopt;
    }
    TextureGpuResource replaced = std::move(found->second);
    found->second = std::move(resource);
    return replaced;
}

std::optional<TextureGpuResource> TextureGpuCache::remove(const TextureGpuCacheKey& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end())
        return std::nullopt;
    TextureGpuResource resource = std::move(found->second);
    entries_.erase(found);
    return resource;
}

std::vector<TextureGpuCache::Entry> TextureGpuCache::extractIf(const Predicate& predicate) {
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

std::vector<TextureGpuCache::Entry> TextureGpuCache::extractAll() {
    return extractIf([](const TextureGpuCacheKey&, const TextureGpuResource&) { return true; });
}

} // namespace engine
