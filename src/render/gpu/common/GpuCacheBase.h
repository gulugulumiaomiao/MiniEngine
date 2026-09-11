#pragma once

#include "render/gpu/common/IGpuCache.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {

// Shared hash-map storage for the GPU caches. Concrete caches only choose the key,
// resource and hash types; the container semantics (replace on insert, extract-and-erase)
// live here so every cache behaves identically. Like IGpuCache this layer never touches
// the RHI: displaced entries are handed back to the owning Manager for release.
template <typename Key, typename Resource, typename Hash = std::hash<Key>>
class GpuCacheBase : public IGpuCache<Key, Resource> {
public:
    using Entry = typename IGpuCache<Key, Resource>::Entry;
    using Predicate = typename IGpuCache<Key, Resource>::Predicate;

    // Outcome of store(): the resident entry plus the resource it displaced, if any.
    struct StoreResult {
        Resource* stored{};
        std::optional<Resource> replaced;
    };

    [[nodiscard]] const Resource* find(const Key& key) const override {
        const auto found = entries_.find(key);
        return found == entries_.end() ? nullptr : &found->second;
    }

    // Inserts or replaces an entry and hands back a pointer to it, sparing callers the
    // extra lookup they would otherwise need to read the resource they just stored.
    [[nodiscard]] StoreResult store(Key key, Resource resource) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            const auto inserted = entries_.emplace(std::move(key), std::move(resource)).first;
            return {&inserted->second, std::nullopt};
        }
        std::optional<Resource> replaced = std::move(found->second);
        found->second = std::move(resource);
        return {&found->second, std::move(replaced)};
    }

    [[nodiscard]] std::optional<Resource> put(Key key, Resource resource) override {
        return store(std::move(key), std::move(resource)).replaced;
    }

    [[nodiscard]] std::optional<Resource> remove(const Key& key) override {
        const auto found = entries_.find(key);
        if (found == entries_.end())
            return std::nullopt;
        std::optional<Resource> resource = std::move(found->second);
        entries_.erase(found);
        return resource;
    }

    [[nodiscard]] std::vector<Entry> extractIf(const Predicate& predicate) override {
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

    [[nodiscard]] std::vector<Entry> extractAll() override {
        std::vector<Entry> result;
        result.reserve(entries_.size());
        for (auto& [key, resource] : entries_)
            result.emplace_back(key, std::move(resource));
        entries_.clear();
        return result;
    }

    [[nodiscard]] std::size_t size() const override { return entries_.size(); }

private:
    std::unordered_map<Key, Resource, Hash> entries_;
};

} // namespace engine
