#include "render/render_graph/RgTexturePool.h"
#include "render/render_graph/RgRenderPassCache.h"
#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace engine {

std::size_t RgTexturePool::KeyHash::operator()(const Key& key) const {
    std::size_t hash = 0;
    const auto add = [&](auto value) {
        hash ^= static_cast<std::size_t>(value) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    };
    add(key.type); add(key.format); add(key.width); add(key.height);
    add(key.depth); add(key.arrayLayers); add(key.mipCount); add(key.usage); add(key.samples);
    add(key.sampler.filterMode); add(key.sampler.addressModeU); add(key.sampler.addressModeV);
    add(std::hash<float>{}(key.sampler.maxAnisotropy));
    return hash;
}

RgTexturePool::~RgTexturePool() {
    if (batchActive_ || activeLeases_)
        Log::fatal("RgTexturePool", "Finish batches and release texture leases before destroying the pool");
    auto* device = RHI_DEVICE;
    if (device && device->uid() == deviceUid_ && !batches_.empty()) {
        collect(device->completedSubmission());
        if (!batches_.empty()) Log::fatal("RgTexturePool", "Wait for GPU submissions before destroying the pool");
    }
}

void RgTexturePool::bindActiveDevice() {
    const auto* device = RHI_DEVICE;
    if (!device) Log::fatal("RgTexturePool", "No active device for pooled textures");
    if (deviceUid_ == device->uid()) return;
    if (batchActive_ || activeLeases_)
        Log::fatal("RgTexturePool", "Finish batches and release texture leases before changing devices");
    // Native teardown belongs to the old device; Texture UID guards protect new RIDs.
    bins_.clear(); batches_.clear(); pendingStates_.clear();
    entryCount_ = 0; lastSubmitted_ = 0; completed_ = 0; epoch_ = 0;
    deviceUid_ = device->uid();
}

RgTexturePool::Entry* RgTexturePool::acquire(const TextureDesc& desc, rhi::TextureUsage usage) {
    if (!toRhiSampleCount(desc.samples) || usage == rhi::TextureUsage::None ||
        !std::isfinite(desc.sampler.maxAnisotropy) || !desc.width || !desc.height ||
        !desc.mipCount || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.type != TextureType::Texture2D)
        Log::fatal("RgTexturePool", "Invalid transient texture description");
    bindActiveDevice();
    const Key key{desc.type, desc.format, desc.width, desc.height, desc.depth,
                  desc.arrayLayers, desc.mipCount, usage, desc.samples, desc.sampler};
    Bin& bin = bins_[key];
    Entry* result{};
    if (!bin.freeEntries.empty()) {
        result = bin.freeEntries.back();
        bin.freeEntries.pop_back();
    } else {
        // Reserve both owning storage and free-list capacity before allocating GPU resources.
        const auto capacity = bin.entries.size() == bin.entries.capacity()
            ? std::max(std::size_t{1}, bin.entries.capacity() * 2) : bin.entries.capacity();
        bin.entries.reserve(capacity);
        bin.freeEntries.reserve(capacity);
        auto entry = createEntry(desc, usage, bin);
        result = entry.get();
        bin.entries.push_back(std::move(entry));
        ++entryCount_;
    }
    result->inUse = true;
    result->status_ = Status::Leased;
    result->lastUsedEpoch_ = epoch_;
    ++activeLeases_;
    return result;
}

void RgTexturePool::makeAvailable(Entry& entry) {
    entry.status_ = Status::Available;
    entry.bin_->freeEntries.push_back(&entry);
}

void RgTexturePool::releaseEntry(Entry& entry) {
    if (!entry.inUse) Log::fatal("RgTexturePool", "Texture lease already released");
    entry.inUse = false;
    --activeLeases_;
    if (entry.status_ == Status::Leased) makeAvailable(entry);
}

void RgTexturePool::release(Entry* entry) {
    if (entry) entry->owner_->releaseEntry(*entry);
}

void RgTexturePool::beginBatch(rhi::RID commandBuffer) {
    bindActiveDevice();
    if (batchActive_) Log::fatal("RgTexturePool", "A submission batch is already active");
    batches_.emplace_back();
    batchActive_ = true;
    batchCommandBuffer_ = commandBuffer;
    ++epoch_;
}

void RgTexturePool::bindCommandBuffer(rhi::RID commandBuffer) {
    if (!batchActive_ || !commandBuffer || (batchCommandBuffer_ && batchCommandBuffer_ != commandBuffer))
        Log::fatal("RgTexturePool", "Execute requires an active batch for this command buffer");
    batchCommandBuffer_ = commandBuffer;
}

void RgTexturePool::recordUsage(Entry* entry, rhi::ResourceState state, rhi::AccessMode access) {
    if (!batchActive_ || !entry || entry->owner_ != this || !entry->inUse ||
        (entry->status_ != Status::Leased && entry->status_ != Status::PendingSubmit))
        Log::fatal("RgTexturePool", "Invalid texture usage in submission batch");
    if (entry->status_ != Status::PendingSubmit) {
        batches_.back().entries.push_back(entry);
        entry->status_ = Status::PendingSubmit;
    }
    entry->pendingState_ = state;
    entry->pendingAccess_ = access;
}

void RgTexturePool::recordState(rhi::ResourceState* state, rhi::ResourceState next) {
    if (!batchActive_ || !state) Log::fatal("RgTexturePool", "Invalid tracked state in submission batch");
    pendingStates_.insert_or_assign(state, next);
}

rhi::ResourceState RgTexturePool::trackedState(rhi::ResourceState* state) const {
    const auto found = pendingStates_.find(state);
    return found == pendingStates_.end() ? *state : found->second;
}

void RgTexturePool::onSubmitted(rhi::SubmissionId submission) {
    bindActiveDevice();
    if (!batchActive_ || !submission || submission <= lastSubmitted_)
        Log::fatal("RgTexturePool", "Invalid submission identity for batch");
    auto& batch = batches_.back();
    batch.submission = submission;
    for (auto* entry : batch.entries) {
        entry->lastState = entry->pendingState_;
        entry->lastAccess = entry->pendingAccess_;
        entry->status_ = Status::InFlight;
    }
    for (const auto& [state, next] : pendingStates_)
        *state = next;
    pendingStates_.clear();
    batchActive_ = false;
    batchCommandBuffer_ = {};
    lastSubmitted_ = submission;
}

void RgTexturePool::cancelBatch() {
    if (!batchActive_) Log::fatal("RgTexturePool", "No submission batch to cancel");
    for (auto* entry : batches_.back().entries) {
        if (entry->inUse) entry->status_ = Status::Leased;
        else makeAvailable(*entry);
    }
    batches_.pop_back();
    pendingStates_.clear();
    batchActive_ = false;
    batchCommandBuffer_ = {};
}

void RgTexturePool::collect(rhi::SubmissionId completed) {
    bindActiveDevice();
    if (completed < completed_) Log::fatal("RgTexturePool", "GPU completion cannot move backwards");
    completed_ = completed;
    while (!batches_.empty() && batches_.front().submission && batches_.front().submission <= completed) {
        for (auto* entry : batches_.front().entries) {
            if (entry->inUse) entry->status_ = Status::Leased;
            else makeAvailable(*entry);
        }
        batches_.pop_front();
    }
}

void RgTexturePool::trim(std::uint64_t maxIdleBatches, RgRenderPassCache& cache) {
    bindActiveDevice();
    for (auto it = bins_.begin(); it != bins_.end();) {
        auto& bin = it->second;
        std::erase_if(bin.entries, [&](const std::unique_ptr<Entry>& entry) {
            if (entry->status_ != Status::Available || entry->texture.useCount() != 1 ||
                epoch_ - entry->lastUsedEpoch_ < maxIdleBatches) return false;
            cache.invalidateFramebuffers(entry->texture->defaultView());
            std::erase(bin.freeEntries, entry.get());
            --entryCount_;
            return true;
        });
        if (bin.entries.empty()) it = bins_.erase(it);
        else ++it;
    }
}

std::unique_ptr<RgTexturePool::Entry> RgTexturePool::createEntry(const TextureDesc& desc, rhi::TextureUsage usage, Bin& bin) {
    auto entry = std::make_unique<Entry>();
    entry->texture = Texture::createWithUsage(desc, usage);
    if (!entry->texture) Log::fatal("RgTexturePool", "Failed to create pooled texture");
    entry->owner_ = this;
    entry->bin_ = &bin;
    return entry;
}
} // namespace engine
