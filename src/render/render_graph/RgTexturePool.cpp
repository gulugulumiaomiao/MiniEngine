#include "render/render_graph/RgTexturePool.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <algorithm>

namespace engine {

RgTexturePool::RgTexturePool(rhi::IDevice& device, std::uint32_t framesInFlight)
    : device_(device), framesInFlight_(framesInFlight), buckets_(framesInFlight_) {}

RgTexturePool::~RgTexturePool() {
    for (Bucket& bucket : buckets_) {
        for (const std::unique_ptr<PooledTexture>& entry : bucket.entries) {
            if (entry->view) {
                device_.destroyTextureView(entry->view);
            }
            if (entry->texture) {
                device_.destroyTexture(entry->texture);
            }
        }
    }
}

void RgTexturePool::beginFrame(std::uint32_t frameIndex) {
    currentFrameIndex_ = frameIndex;
    resetBucket(frameIndex);
}

void RgTexturePool::resetBucket(std::uint32_t frameIndex) {
    Bucket& bucket = buckets_[frameIndex % framesInFlight_];
    for (std::unique_ptr<PooledTexture>& entry : bucket.entries) {
        entry->inUse = false;
    }
}

RgTexturePool::PooledTexture* RgTexturePool::acquire(const RgTextureDesc& desc) {
    if (desc.format == rhi::TextureFormat::Undefined || desc.width == 0 || desc.height == 0 ||
        desc.mipCount == 0) {
        Log::fatal("RgTexturePool", "Invalid transient texture description");
    }

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
        count += std::ranges::count_if(bucket.entries, [](const std::unique_ptr<PooledTexture>& e) {
            return e->inUse;
        });
    }
    return count;
}

RgTexturePool::Bucket& RgTexturePool::currentBucket() {
    return buckets_[currentFrameIndex_ % framesInFlight_];
}

const RgTexturePool::Bucket& RgTexturePool::currentBucket() const {
    return buckets_[currentFrameIndex_ % framesInFlight_];
}

bool RgTexturePool::matches(const PooledTexture& entry, const RgTextureDesc& desc) {
    return entry.format == desc.format && entry.width == desc.width &&
           entry.height == desc.height && entry.mipCount == desc.mipCount &&
           entry.usage == desc.usage;
}

std::unique_ptr<RgTexturePool::PooledTexture> RgTexturePool::createEntry(const RgTextureDesc& desc) {
    auto entry = std::make_unique<PooledTexture>();

    rhi::TextureDesc textureDesc;
    textureDesc.dimension = desc.dimension;
    textureDesc.format = desc.format;
    textureDesc.width = desc.width;
    textureDesc.height = desc.height;
    textureDesc.depth = desc.depth;
    textureDesc.mipCount = desc.mipCount;
    textureDesc.usage = desc.usage;
    textureDesc.debugName = desc.debugName;

    entry->texture = device_.createTexture(textureDesc);
    if (!entry->texture) {
        Log::fatal("RgTexturePool", "Failed to create pooled texture");
    }

    const rhi::TextureViewDesc viewDesc{.texture = entry->texture,
                                        .format = desc.format,
                                        .aspect = desc.aspect,
                                        .baseMipLevel = 0,
                                        .mipCount = desc.mipCount};
    entry->view = device_.createTextureView(viewDesc);
    if (!entry->view) {
        device_.destroyTexture(entry->texture);
        Log::fatal("RgTexturePool", "Failed to create pooled texture view");
    }

    entry->format = desc.format;
    entry->width = desc.width;
    entry->height = desc.height;
    entry->mipCount = desc.mipCount;
    entry->usage = desc.usage;
    return entry;
}

} // namespace engine
