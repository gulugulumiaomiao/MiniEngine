#include "render/gpu/material/MaterialStorageCache.h"

#include "core/logging/Log.h"

#include <algorithm>

namespace engine {

bool MaterialStorageCache::initialize(std::uint32_t frameCount, std::uint32_t capacity) {
    if (frameCount == 0 || !frames_.empty() || capacity == 0)
        return false;
    frames_.resize(frameCount);
    capacity_ = capacity;
    return true;
}

void MaterialStorageCache::beginFrame(std::uint32_t frameIndex) {
    if (frameIndex >= frames_.size())
        Log::fatal("MaterialStorageCache", "Invalid frame index");
    // The lookup table is intentionally kept: slots stay resident across frames
    // so unchanged materials hit the fast path without any GPU work.
    currentFrame_ = frameIndex;
}

MaterialStorageCacheSlot MaterialStorageCache::acquire(std::uint64_t materialKey) {
    if (frames_.empty())
        Log::fatal("MaterialStorageCache", "Cache is not initialized");
    Frame& current = frames_[currentFrame_];
    if (const auto found = current.lookup.find(materialKey); found != current.lookup.end()) {
        MaterialStorageEntry& resource = current.resources[found->second];
        resource.lastUsed = ++lruStamp_;
        return {&resource, true};
    }

    std::uint32_t index{};
    if (current.resources.size() < capacity_) {
        // Grow up to the capacity.
        index = static_cast<std::uint32_t>(current.resources.size());
        current.resources.emplace_back();
    } else {
        // Capacity exhausted: evict the least recently used slot.
        const auto victim = std::min_element(current.resources.begin(),
                                             current.resources.end(),
                                             [](const MaterialStorageEntry& lhs,
                                                const MaterialStorageEntry& rhs) {
                                                 return lhs.lastUsed < rhs.lastUsed;
                                             });
        if (victim == current.resources.end())
            Log::fatal("MaterialStorageCache", "Cache has no slots to evict");
        index = static_cast<std::uint32_t>(victim - current.resources.begin());
        current.lookup.erase(victim->ownerKey);
        victim->pendingRelease = true; // Manager must free the GPU handles before reuse.
    }
    MaterialStorageEntry& resource = current.resources[index];
    resource.ownerKey = materialKey;
    resource.lastUsed = ++lruStamp_;
    current.lookup.emplace(materialKey, index);
    return {&resource, false};
}

std::vector<MaterialStorageEntry> MaterialStorageCache::extract(std::uint64_t materialKey) {
    std::vector<MaterialStorageEntry> result;
    for (Frame& frame : frames_) {
        const auto found = frame.lookup.find(materialKey);
        if (found == frame.lookup.end())
            continue;
        MaterialStorageEntry& resource = frame.resources[found->second];
        result.push_back(std::move(resource));
        resource = {};
        frame.lookup.erase(found);
    }
    return result;
}

std::vector<MaterialStorageEntry> MaterialStorageCache::extractAll() {
    std::vector<MaterialStorageEntry> result;
    result.reserve(size());
    for (Frame& frame : frames_) {
        for (MaterialStorageEntry& resource : frame.resources)
            result.push_back(std::move(resource));
        frame.resources.clear();
        frame.lookup.clear();
    }
    return result;
}

std::size_t MaterialStorageCache::size() const {
    std::size_t result{};
    for (const Frame& frame : frames_)
        result += frame.resources.size();
    return result;
}

void MaterialStorageCache::reset() {
    frames_.clear();
    currentFrame_ = 0;
    capacity_ = 0;
    lruStamp_ = 0;
}

} // namespace engine
