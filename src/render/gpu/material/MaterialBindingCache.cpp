#include "render/gpu/material/MaterialBindingCache.h"

#include "core/logging/Log.h"

namespace engine {

bool MaterialBindingCache::initialize(std::uint32_t frameCount) {
    if (frameCount == 0 || !frames_.empty())
        return false;
    frames_.resize(frameCount);
    return true;
}

void MaterialBindingCache::beginFrame(std::uint32_t frameIndex) {
    if (frameIndex >= frames_.size())
        Log::fatal("MaterialBindingCache", "Invalid frame index");
    currentFrame_ = frameIndex;
    Frame& frame = frames_[frameIndex];
    frame.used = 0;
    frame.lookup.clear();
}

MaterialBindingCacheSlot MaterialBindingCache::acquire(std::uint64_t materialKey) {
    if (frames_.empty())
        Log::fatal("MaterialBindingCache", "Cache is not initialized");
    Frame& frame = frames_[currentFrame_];
    if (const auto found = frame.lookup.find(materialKey); found != frame.lookup.end())
        return {&frame.resources[found->second], true};

    const std::uint32_t index = frame.used++;
    if (index == frame.resources.size())
        frame.resources.emplace_back();
    frame.lookup.emplace(materialKey, index);
    return {&frame.resources[index], false};
}

std::vector<MaterialGpuResource> MaterialBindingCache::extractAll() {
    std::vector<MaterialGpuResource> result;
    result.reserve(size());
    for (Frame& frame : frames_) {
        for (MaterialGpuResource& resource : frame.resources)
            result.push_back(std::move(resource));
        frame.resources.clear();
        frame.lookup.clear();
        frame.used = 0;
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
}

} // namespace engine
