#include "render/uniform/UniformSetLayout.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <utility>

namespace engine {
namespace {

[[nodiscard]] std::vector<ResourceBinding> sortedCopy(std::span<const ResourceBinding> bindings) {
    std::vector<ResourceBinding> sorted(bindings.begin(), bindings.end());
    std::ranges::sort(sorted, [](const ResourceBinding& a, const ResourceBinding& b) {
        return a.binding < b.binding;
    });
    return sorted;
}

// Binding index + descriptor type in ascending binding order.
[[nodiscard]] Hash64 orderedHashOf(const std::vector<ResourceBinding>& sorted) {
    Hash64 hash = kFnv1a64OffsetBasis;
    for (const ResourceBinding& binding : sorted) {
        hashAppend(hash, binding.binding);
        hashAppend(hash, binding.type);
    }
    return hash;
}

// Per-descriptor-type counts, keyed by type in ascending enum order (binding-order independent).
[[nodiscard]] Hash64 unorderedHashOf(const std::vector<ResourceBinding>& sorted) {
    std::vector<std::pair<rhi::ResourceType, std::uint32_t>> counts;
    for (const ResourceBinding& binding : sorted) {
        const auto found = std::ranges::find_if(
            counts, [&binding](const auto& entry) { return entry.first == binding.type; });
        if (found != counts.end())
            ++found->second;
        else
            counts.emplace_back(binding.type, 1U);
    }
    std::ranges::sort(counts, [](const auto& a, const auto& b) { return a.first < b.first; });
    Hash64 hash = kFnv1a64OffsetBasis;
    for (const auto& [type, count] : counts) {
        hashAppend(hash, type);
        hashAppend(hash, count);
    }
    return hash;
}

} // namespace

std::unordered_map<Hash64, Ref<UniformSetLayout>> UniformSetLayout::cache_;
std::uint64_t UniformSetLayout::cacheDeviceUid_ = 0;

Ref<UniformSetLayout> UniformSetLayout::acquire(std::span<const ResourceBinding> bindings) {
    rhi::IDevice* device = RHI_DEVICE;
    if (!device) {
        Log::error("UniformSetLayout", "No active device to acquire a set layout");
        return {};
    }
    const std::vector<ResourceBinding> sorted = sortedCopy(bindings);
    const Hash64 ordered = orderedHashOf(sorted);
    if (cacheDeviceUid_ != device->uid()) {
        cache_.clear();
        cacheDeviceUid_ = device->uid();
    }
    if (const auto found = cache_.find(ordered); found != cache_.end())
        return found->second;

    Ref<UniformSetLayout> layout = makeRef<UniformSetLayout>();
    layout->begin();
    for (const ResourceBinding& binding : sorted)
        layout->addBinding(binding);
    layout->end();
    cache_.emplace(ordered, layout);
    return layout;
}

void UniformSetLayout::clearCache() {
    cache_.clear();
    cacheDeviceUid_ = 0;
}

UniformSetLayout::UniformSetLayout() {
    rhi::IDevice* device = RHI_DEVICE;
    if (!device) {
        Log::error("UniformSetLayout", "No active device for set-layout creation");
        return;
    }
    deviceUid_ = device->uid();
    rid_ = device->descriptor_set_layout_allocate_rid();
}

UniformSetLayout::~UniformSetLayout() {
    // Cross-device safety: only recycle the RID if the creating device is still the active one.
    if (rhi::IDevice* device = RHI_DEVICE; rid_ && device && device->uid() == deviceUid_) {
        device->descriptor_set_layout_destroy(rid_);
    }
    rid_ = {};
}

void UniformSetLayout::begin() {
    bindings_.clear();
}

void UniformSetLayout::addBinding(ResourceBinding binding) {
    bindings_.push_back(std::move(binding));
}

void UniformSetLayout::end() {
    std::ranges::sort(bindings_, [](const ResourceBinding& a, const ResourceBinding& b) {
        return a.binding < b.binding;
    });
    orderedHash_ = orderedHashOf(bindings_);
    unorderedHash_ = unorderedHashOf(bindings_);

    std::vector<rhi::ResourceBindingDesc> descs;
    descs.reserve(bindings_.size());
    for (const ResourceBinding& binding : bindings_)
        descs.push_back({binding.binding, binding.name, binding.type});
    if (rhi::IDevice* device = RHI_DEVICE)
        device->descriptor_set_layout_build(rid_, rhi::DescriptorSetLayoutDesc{descs, "UniformSetLayout"});
}

} // namespace engine
