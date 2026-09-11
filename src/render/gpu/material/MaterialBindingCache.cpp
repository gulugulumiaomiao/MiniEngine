#include "render/gpu/material/MaterialBindingCache.h"

#include "core/logging/Log.h"

#include <algorithm>

namespace engine {

bool MaterialBindingCache::initialize(std::uint32_t frameCount, std::uint32_t capacity) {
    if (frameCount == 0 || !frames_.empty() || capacity == 0)
        return false;
    frames_.resize(frameCount);
    capacity_ = capacity;
    return true;
}

void MaterialBindingCache::beginFrame(std::uint32_t frameIndex) {
    if (frameIndex >= frames_.size())
        Log::fatal("MaterialBindingCache", "Invalid frame index");
    // The lookup table is intentionally kept: slots stay resident across frames
    // so unchanged materials hit the fast path without any GPU work.
    currentFrame_ = frameIndex;
}

MaterialBindingCacheSlot MaterialBindingCache::acquire(std::uint64_t materialKey) {
    if (frames_.empty())
        Log::fatal("MaterialBindingCache", "Cache is not initialized");
    Frame& current = frames_[currentFrame_];
    if (const auto found = current.lookup.find(materialKey); found != current.lookup.end()) {
        MaterialGpuResource& resource = current.resources[found->second];
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
                                             [](const MaterialGpuResource& lhs,
                                                const MaterialGpuResource& rhs) {
                                                 return lhs.lastUsed < rhs.lastUsed;
                                             });
        if (victim == current.resources.end())
            Log::fatal("MaterialBindingCache", "Cache has no slots to evict");
        index = static_cast<std::uint32_t>(victim - current.resources.begin());
        current.lookup.erase(victim->ownerKey);
        victim->pendingRelease = true; // Manager must free the GPU handles before reuse.
    }
    MaterialGpuResource& resource = current.resources[index];
    resource.ownerKey = materialKey;
    resource.lastUsed = ++lruStamp_;
    current.lookup.emplace(materialKey, index);
    return {&resource, false};
}

std::vector<MaterialGpuResource> MaterialBindingCache::extractAll() {
    std::vector<MaterialGpuResource> result;
    result.reserve(size());
    for (Frame& frame : frames_) {
        for (MaterialGpuResource& resource : frame.resources)
            result.push_back(std::move(resource));
        frame.resources.clear();
        frame.lookup.clear();
    }
    return result;
}

std::size_t MaterialBindingCache::size() const {
    std::size_t result{};
    for (const Frame& frame : frames_)
        result += frame.resources.size();
    return result;
}

void MaterialBindingCache::reset() {
    frames_.clear();
    currentFrame_ = 0;
    capacity_ = 0;
    lruStamp_ = 0;
}

} // namespace engine
