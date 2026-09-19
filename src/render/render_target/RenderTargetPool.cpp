#include "render/render_target/RenderTargetPool.h"

#include "core/logging/Log.h"

#include <algorithm>

namespace engine {

RenderTargetPool::RenderTargetPool(rhi::IDevice& device, std::uint32_t framesInFlight)
    : device_(device), retireBuckets_(framesInFlight) {
    if (framesInFlight == 0) {
        Log::fatal("RenderTargetPool", "At least one in-flight frame is required");
    }
}

void RenderTargetPool::beginFrame(std::uint32_t frameIndex) {
    currentFrameIndex_ = frameIndex % static_cast<std::uint32_t>(retireBuckets_.size());
    std::vector<RenderTargetHandle>& completed = retireBuckets_[currentFrameIndex_];
    for (const RenderTargetHandle handle : completed) {
        (void)targets_.release(handle);
    }
    completed.clear();
}

RenderTargetHandle RenderTargetPool::acquire(RenderTargetDesc desc) {
    const RenderTargetHandle handle = targets_.emplace(device_);
    RenderTarget* target = targets_.find(handle);
    if (!target->create(std::move(desc))) {
        (void)targets_.release(handle);
        return {};
    }
    return handle;
}

void RenderTargetPool::release(RenderTargetHandle handle) {
    if (!targets_.find(handle) || isRetired(handle)) {
        return;
    }
    retireBuckets_[currentFrameIndex_].push_back(handle);
}

RenderTarget* RenderTargetPool::find(RenderTargetHandle handle) {
    return isRetired(handle) ? nullptr : targets_.find(handle);
}

const RenderTarget* RenderTargetPool::find(RenderTargetHandle handle) const {
    return isRetired(handle) ? nullptr : targets_.find(handle);
}

std::size_t RenderTargetPool::activeCount() const {
    return targets_.size() - retiredCount();
}

std::size_t RenderTargetPool::retiredCount() const {
    std::size_t count{};
    for (const auto& bucket : retireBuckets_) {
        count += bucket.size();
    }
    return count;
}

bool RenderTargetPool::isRetired(RenderTargetHandle handle) const {
    return std::ranges::any_of(retireBuckets_, [handle](const auto& bucket) {
        return std::ranges::find(bucket, handle) != bucket.end();
    });
}

} // namespace engine
