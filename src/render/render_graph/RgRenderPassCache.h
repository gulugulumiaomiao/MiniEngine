#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine {

// Persistent render-graph resources shared by successive frame-local graphs.
// The rendering layer owns reuse and invalidation; the RHI only creates/destroys RIDs.
// This cache must outlive submitted graphs. Retire the affected GPU work before
// invalidating framebuffers, clearing the cache, or destroying it.
class RgRenderPassCache final {
public:
    RgRenderPassCache() = default;
    ~RgRenderPassCache();

    RgRenderPassCache(const RgRenderPassCache&) = delete;
    RgRenderPassCache& operator=(const RgRenderPassCache&) = delete;
    RgRenderPassCache(RgRenderPassCache&&) = delete;
    RgRenderPassCache& operator=(RgRenderPassCache&&) = delete;

    [[nodiscard]] rhi::RID getOrCreateRenderPass(const rhi::RenderPassDesc& desc);
    [[nodiscard]] rhi::RID getOrCreateFramebuffer(const rhi::FramebufferDesc& desc);

    // Call before destroying/replacing an imported view, after its frame fence.
    void invalidateFramebuffers(rhi::RID view);
    // Call after device idle for swapchain recreation; compatible render passes survive.
    void clearFramebuffers();
    void clear();

    [[nodiscard]] std::size_t renderPassCount() const { return renderPassCache_.size(); }
    [[nodiscard]] std::size_t framebufferCount() const { return framebufferCache_.size(); }

private:
    void bindActiveDevice();

    struct RenderPassEntry {
        rhi::RenderPassDesc desc;
        rhi::RID handle;
    };
    struct FramebufferEntry {
        rhi::FramebufferDesc desc;
        rhi::RID handle;
    };
    std::vector<RenderPassEntry> renderPassCache_;
    std::vector<FramebufferEntry> framebufferCache_;
    std::uint64_t deviceUid_{};
};

} // namespace engine
