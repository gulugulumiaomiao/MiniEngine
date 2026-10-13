#include "render/render_graph/RgTexturePool.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <algorithm>

namespace engine {

RgTexturePool::RgTexturePool(std::uint32_t framesInFlight)
    : framesInFlight_(framesInFlight), buckets_(framesInFlight_) {
    if (!framesInFlight_) Log::fatal("RgTexturePool", "At least one frame slot is required");
}

RgTexturePool::~RgTexturePool() {
    rhi::IDevice* device = RHI_DEVICE;
    for (Bucket& bucket : buckets_) {
        for (const std::unique_ptr<PooledTexture>& entry : bucket.entries) {
            if (entry->texture && device && device->uid() == deviceUid_)
                device->texture_destroy(entry->texture);
        }
    }
}

void RgTexturePool::beginFrame(std::uint32_t frameIndex) {
    const auto& bucket = buckets_[frameIndex % framesInFlight_];
    for (const auto& entry : bucket.entries)
        if (entry->inUse) Log::fatal("RgTexturePool", "Release texture leases before reusing a frame slot");
    currentFrameIndex_ = frameIndex;
}

void RgTexturePool::bindActiveDevice() {
    const auto* device = RHI_DEVICE;
    if (!device) Log::fatal("RgTexturePool", "No active device for pooled textures");
    if (deviceUid_ == device->uid()) return;
    if (inUseCount()) Log::fatal("RgTexturePool", "Release texture leases before changing devices");
    // Old device pools own native teardown. Never reuse or destroy their RIDs on a new device.
    for (auto& bucket : buckets_) bucket.entries.clear();
    deviceUid_ = device->uid();
}

RgTexturePool::PooledTexture* RgTexturePool::acquire(const RgTextureDesc& desc) {
    if (desc.format == rhi::PixelFormat::Undefined || desc.width == 0 || desc.height == 0 ||
        desc.mipCount == 0 || desc.depth == 0 || desc.arrayLayers == 0 ||
        desc.dimension != rhi::TextureType::Texture2D) {
        Log::fatal("RgTexturePool", "Invalid transient texture description");
    }

    bindActiveDevice();
    Bucket& bucket = currentBucket();
    for (std::unique_ptr<PooledTexture>& entry : bucket.entries) {
        if (!entry->inUse && matches(*entry, desc)) {
            entry->inUse = true;
            return entry.get();
        }
    }

    std::unique_ptr<PooledTexture> entry = createEntry(desc);
    PooledTexture* result = entry.get();
    result->inUse = true;
    bucket.entries.push_back(std::move(entry));
    return result;
}

void RgTexturePool::release(PooledTexture* entry) {
    if (entry) {
        entry->inUse = false;
    }
}

std::size_t RgTexturePool::totalEntryCount() const {
    std::size_t count = 0;
    for (const Bucket& bucket : buckets_) {
        count += bucket.entries.size();
    }
    return count;
}

std::size_t RgTexturePool::inUseCount() const {
    std::size_t count = 0;
    for (const Bucket& bucket : buckets_) {
        count += std::ranges::count_if(
            bucket.entries, [](const std::unique_ptr<PooledTexture>& e) { return e->inUse; });
    }
    return count;
}

RgTexturePool::Bucket& RgTexturePool::currentBucket() {
    return buckets_[currentFrameIndex_ % framesInFlight_];
}

bool RgTexturePool::matches(const PooledTexture& entry, const RgTextureDesc& desc) {
    return entry.format == desc.format && entry.width == desc.width &&
           entry.height == desc.height && entry.depth == desc.depth &&
           entry.arrayLayers == desc.arrayLayers && entry.mipCount == desc.mipCount &&
           entry.usage == desc.usage && entry.samples == desc.samples;
}

std::unique_ptr<RgTexturePool::PooledTexture>
RgTexturePool::createEntry(const RgTextureDesc& desc) {
    rhi::IDevice* device = RHI_DEVICE;
    if (!device)
        Log::fatal("RgTexturePool", "No active device for pooled texture creation");
    auto entry = std::make_unique<PooledTexture>();

    rhi::TextureDesc textureDesc;
    textureDesc.dimension = desc.dimension;
    textureDesc.format = desc.format;
    textureDesc.width = desc.width;
    textureDesc.height = desc.height;
    textureDesc.depth = desc.depth;
    textureDesc.arrayLayers = desc.arrayLayers;
    textureDesc.mipCount = desc.mipCount;
    textureDesc.usage = desc.usage;
    textureDesc.samples = desc.samples;
    textureDesc.debugName = desc.debugName;

    entry->texture = device->texture_create(textureDesc);
    if (!entry->texture) {
        Log::fatal("RgTexturePool", "Failed to create pooled texture");
    }

    entry->view = device->texture_default_view(entry->texture);
    if (!entry->view) {
        device->texture_destroy(entry->texture);
        Log::fatal("RgTexturePool", "Failed to create pooled texture view");
    }

    entry->format = desc.format;
    entry->width = desc.width;
    entry->height = desc.height;
    entry->depth = desc.depth;
    entry->arrayLayers = desc.arrayLayers;
    entry->mipCount = desc.mipCount;
    entry->usage = desc.usage;
    entry->samples = desc.samples;
    return entry;
}

} // namespace engine
