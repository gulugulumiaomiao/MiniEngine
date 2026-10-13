#pragma once

#include "render/render_graph/RgTypes.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

// Frame-indexed texture pool for RenderGraph transient textures.
//
// One bucket per in-flight frame. Reuse a bucket only after its fence completes
// and all CPU leases are released.
class RgTexturePool final {
public:
    struct PooledTexture {
        rhi::RID texture;
        rhi::RID view;
        rhi::PixelFormat format{rhi::PixelFormat::Undefined};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t depth{1};
        std::uint32_t arrayLayers{1};
        std::uint32_t mipCount{1};
        rhi::TextureUsage usage{rhi::TextureUsage::None};
        rhi::SampleCount samples{rhi::SampleCount::One};
        bool inUse{false};
        rhi::ResourceState lastState{rhi::ResourceState::Undefined};
        rhi::AccessMode lastAccess{rhi::AccessMode::Read};
    };

    explicit RgTexturePool(std::uint32_t framesInFlight = 2);
    ~RgTexturePool();

    RgTexturePool(const RgTexturePool&) = delete;
    RgTexturePool& operator=(const RgTexturePool&) = delete;
    RgTexturePool(RgTexturePool&&) = delete;
    RgTexturePool& operator=(RgTexturePool&&) = delete;

    // Call after the slot's fence completes and all of its leases are released.
    void beginFrame(std::uint32_t frameIndex);

    // Acquire a pooled texture matching the description for the current frame.
    [[nodiscard]] PooledTexture* acquire(const RgTextureDesc& desc);

    // Release a live entry without retaining its pool instance. Safe with nullptr.
    static void release(PooledTexture* entry);

    [[nodiscard]] std::uint32_t framesInFlight() const { return framesInFlight_; }
    [[nodiscard]] std::uint32_t currentFrameIndex() const { return currentFrameIndex_; }
    [[nodiscard]] std::size_t totalEntryCount() const;
    [[nodiscard]] std::size_t inUseCount() const;

private:
    struct Bucket {
        std::vector<std::unique_ptr<PooledTexture>> entries;
    };

    [[nodiscard]] Bucket& currentBucket();
    void bindActiveDevice();
    [[nodiscard]] static bool matches(const PooledTexture& entry, const RgTextureDesc& desc);
    [[nodiscard]] std::unique_ptr<PooledTexture> createEntry(const RgTextureDesc& desc);

    std::uint32_t framesInFlight_{2};
    std::uint32_t currentFrameIndex_{0};
    std::vector<Bucket> buckets_;
    std::uint64_t deviceUid_{};
};

} // namespace engine
