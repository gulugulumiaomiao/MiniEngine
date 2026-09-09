#pragma once

#include "core/base/HandlePool.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace engine {

// Owns resources in a HandlePool and maintains one primary Key -> Handle index.
// A resource's key must not change while the resource is registered.
template <typename Resource,
          typename HandleType,
          typename Key,
          typename KeyHash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
class KeyedHandleRegistry {
public:
    virtual ~KeyedHandleRegistry() = default;

    [[nodiscard]] HandleType insert(Resource resource) {
        if (!validate(resource))
            return {};

        const Key key = keyOf(resource);
        if (const HandleType existing = findHandle(key); existing)
            return existing;

        const HandleType handle = pool_.insert(std::move(resource));
        keyIndex_.insert_or_assign(key, handle);
        return handle;
    }

    [[nodiscard]] Resource* find(HandleType handle) { return pool_.find(handle); }

    [[nodiscard]] const Resource* find(HandleType handle) const { return pool_.find(handle); }

    [[nodiscard]] Resource* find(const Key& key) {
        return const_cast<Resource*>(std::as_const(*this).find(key));
    }

    [[nodiscard]] const Resource* find(const Key& key) const {
        return pool_.find(findHandle(key));
    }

    [[nodiscard]] HandleType findHandle(const Key& key) const {
        const auto indexed = keyIndex_.find(key);
        if (indexed == keyIndex_.end() || !pool_.find(indexed->second))
            return {};
        return indexed->second;
    }

    [[nodiscard]] bool destroy(HandleType handle) {
        Resource* resource = pool_.find(handle);
        if (!resource)
            return false;

        const Key key = keyOf(*resource);
        const auto indexed = keyIndex_.find(key);
        if (indexed != keyIndex_.end() && indexed->second == handle)
            keyIndex_.erase(indexed);
        return pool_.release(handle);
    }

    [[nodiscard]] bool destroy(const Key& key) {
        const HandleType handle = findHandle(key);
        return handle && destroy(handle);
    }

    virtual void clear() {
        keyIndex_.clear();
        pool_.clear();
    }

    [[nodiscard]] std::size_t size() const { return pool_.size(); }

protected:
    KeyedHandleRegistry() = default;

    template <typename Function> void forEach(Function&& function) {
        pool_.forEach(std::forward<Function>(function));
    }

    template <typename Function> void forEach(Function&& function) const {
        pool_.forEach(std::forward<Function>(function));
    }

    template <typename Function> void forEachEntry(Function&& function) {
        for (const auto& [key, handle] : keyIndex_) {
            if (Resource* resource = pool_.find(handle))
                function(key, handle, *resource);
        }
    }

    template <typename Function> void forEachEntry(Function&& function) const {
        for (const auto& [key, handle] : keyIndex_) {
            if (const Resource* resource = pool_.find(handle))
                function(key, handle, *resource);
        }
    }

    [[nodiscard]] virtual Key keyOf(const Resource& resource) const = 0;
    [[nodiscard]] virtual bool validate(const Resource&) const { return true; }

private:
    HandlePool<Resource, HandleType> pool_;
    std::unordered_map<Key, HandleType, KeyHash, KeyEqual> keyIndex_;
};

} // namespace engine
