#include "render/uniform/PipelineLayout.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <utility>

namespace engine {
namespace {

// Ordered combination of each set layout's orderedHash (plus the set count), so the sequence of
// sets uniquely keys the pipeline layout.
[[nodiscard]] Hash64 pipelineLayoutHash(const std::vector<Ref<UniformSetLayout>>& setLayouts) {
    Hash64 hash = kFnv1a64OffsetBasis;
    hashAppend(hash, static_cast<std::uint64_t>(setLayouts.size()));
    for (const Ref<UniformSetLayout>& layout : setLayouts)
        hashAppend(hash, layout ? layout->orderedHash() : Hash64{});
    return hash;
}

} // namespace

std::unordered_map<Hash64, Ref<PipelineLayout>> PipelineLayout::cache_;
std::uint64_t PipelineLayout::cacheDeviceUid_ = 0;

Ref<PipelineLayout> PipelineLayout::acquire(std::vector<Ref<UniformSetLayout>> setLayouts) {
    rhi::IDevice* device = RHI_DEVICE;
    if (!device) {
        Log::error("PipelineLayout", "No active device to acquire a pipeline layout");
        return {};
    }
    const Hash64 key = pipelineLayoutHash(setLayouts);
    if (cacheDeviceUid_ != device->uid()) {
        cache_.clear();
        cacheDeviceUid_ = device->uid();
    }
    if (const auto found = cache_.find(key); found != cache_.end())
        return found->second;

    Ref<PipelineLayout> layout = makeRef<PipelineLayout>(std::move(setLayouts));
    layout->create();
    cache_.emplace(key, layout);
    return layout;
}

void PipelineLayout::clearCache() {
    cache_.clear();
    cacheDeviceUid_ = 0;
}

PipelineLayout::PipelineLayout(std::vector<Ref<UniformSetLayout>> setLayouts)
    : setLayouts_(std::move(setLayouts)) {
    if (rhi::IDevice* device = RHI_DEVICE)
        deviceUid_ = device->uid();
    hash_ = pipelineLayoutHash(setLayouts_);
}

PipelineLayout::~PipelineLayout() {
    // Cross-device safety: only destroy the layout if the creating device is still the active one.
    // The referenced set layouts are released by their own Ref members (which guard likewise).
    if (rhi::IDevice* device = RHI_DEVICE; rid_ && device && device->uid() == deviceUid_) {
        device->pipeline_layout_destroy(rid_);
    }
    rid_ = {};
}

void PipelineLayout::create() {
    if (rid_)
        return;
    std::vector<rhi::RID> setRids;
    setRids.reserve(setLayouts_.size());
    for (const Ref<UniformSetLayout>& layout : setLayouts_)
        setRids.push_back(layout ? layout->rid() : rhi::RID{});
    if (rhi::IDevice* device = RHI_DEVICE)
        rid_ = device->pipeline_layout_create(rhi::PipelineLayoutDesc{setRids, "PipelineLayout"});
}

} // namespace engine
