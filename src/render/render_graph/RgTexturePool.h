#pragma once

#include "render/render_graph/RgTypes.h"
#include "rhi/api/RhiTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// Frame-indexed texture pool for RenderGraph transient textures.
//
// The pool keeps one bucket per in-flight frame. At the beginning of each frame the bucket that
// was used two frames ago is reset, making its entries available again. This guarantees that a
// transient texture is never reused while the GPU may still be reading it.
class RgTexturePool final {
public:
    struct PooledTexture {
        rhi::TextureHandle texture;
        rhi::TextureViewHandle view;
        rhi::TextureFormat format{rhi::TextureFormat::Undefined};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint32_t mipCount{1};
        rhi::TextureUsage usage{rhi::TextureUsage::None};
        bool inUse{false};
    };

    explicit RgTexturePool(rhi::IDevice& device, std::uint32_t framesInFlight = 2);
    ~RgTexturePool();

    RgTexturePool(const RgTexturePool&) = delete;
    RgTexturePool& operator=(const RgTexturePool&) = delete;
    RgTexturePool(RgTexturePool&&) = delete;
    RgTexturePool& operator=(RgTexturePool&&) = delete;

    // Must be called at the start of each frame before any acquire().
    void beginFrame(std::uint32_t frameIndex);

    // Acquire a pooled texture matching the description for the current frame.
    [[nodiscard]] PooledTexture* acquire(const RgTextureDesc& desc);

    // Release an entry back to the pool. Safe to call with nullptr.
    void release(PooledTexture* entry);

    // Reset a specific bucket (mainly for testing).
    void resetBucket(std::uint32_t frameIndex);

    [[nodiscard]] std::uint32_t framesInFlight() const { return framesInFlight_; }
    [[nodiscard]] std::uint32_t currentFrameIndex() const { return currentFrameIndex_; }
    [[nodiscard]] std::size_t totalEntryCount() const;
    [[nodiscard]] std::size_t inUseCount() const;

private:
    struct Bucket {
        std::vector<std::unique_ptr<PooledTexture>> entries;
    };

    [[nodiscard]] Bucket& currentBucket();
    [[nodiscard]] const Bucket& currentBucket() const;
    [[nodiscard]] static bool matches(const PooledTexture& entry, const RgTextureDesc& desc);
    [[nodiscard]] std::unique_ptr<PooledTexture> createEntry(const RgTextureDesc& desc);

    rhi::IDevice& device_;
    std::uint32_t framesInFlight_{2};
    std::uint32_t currentFrameIndex_{0};
    std::vector<Bucket> buckets_;
};

} // namespace engine
