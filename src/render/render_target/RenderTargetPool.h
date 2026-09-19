#pragma once

#include "core/base/HandlePool.h"
#include "render/base/RenderHandle.h"
#include "render/render_target/RenderTarget.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine {

// Owns every persistent render target. Released targets stay alive until the in-flight
// frame slot that retired them is acquired again, so resize and camera changes cannot
// destroy images still referenced by submitted command buffers.
class RenderTargetPool final {
public:
    explicit RenderTargetPool(rhi::IDevice& device, std::uint32_t framesInFlight = 2);
    ~RenderTargetPool() = default;

    RenderTargetPool(const RenderTargetPool&) = delete;
    RenderTargetPool& operator=(const RenderTargetPool&) = delete;

    void beginFrame(std::uint32_t frameIndex);
    [[nodiscard]] RenderTargetHandle acquire(RenderTargetDesc desc);
    void release(RenderTargetHandle handle);

    [[nodiscard]] RenderTarget* find(RenderTargetHandle handle);
    [[nodiscard]] const RenderTarget* find(RenderTargetHandle handle) const;
    [[nodiscard]] std::size_t activeCount() const;
    [[nodiscard]] std::size_t retiredCount() const;

private:
    [[nodiscard]] bool isRetired(RenderTargetHandle handle) const;

    rhi::IDevice& device_;
    HandlePool<RenderTarget, RenderTargetHandle> targets_;
    std::vector<std::vector<RenderTargetHandle>> retireBuckets_;
    std::uint32_t currentFrameIndex_{};
};

} // namespace engine
