#include "render/render_graph/RgRenderPassCache.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <ranges>

namespace engine {

RgRenderPassCache::~RgRenderPassCache() { clear(); }

void RgRenderPassCache::bindActiveDevice() {
    const auto* device = RHI_DEVICE;
    if (!device) Log::fatal("RgRenderPassCache", "No active device for render-graph cache");
    if (deviceUid_ == device->uid()) return;
    // The old device's resource pools clean up their objects at teardown. Never
    // destroy an old RID through a different device that might have reused its value.
    framebufferCache_.clear();
    renderPassCache_.clear();
    deviceUid_ = device->uid();
}

rhi::RID RgRenderPassCache::getOrCreateRenderPass(const rhi::RenderPassDesc& desc) {
    bindActiveDevice();
    for (const auto& entry : renderPassCache_)
        if (entry.desc == desc) return entry.handle;
    const auto handle = RHI_DEVICE->render_pass_create(desc);
    if (!handle) Log::fatal("RgRenderPassCache", "Render pass creation failed or unsupported");
    renderPassCache_.push_back({desc, handle});
    return handle;
}

rhi::RID RgRenderPassCache::getOrCreateFramebuffer(const rhi::FramebufferDesc& desc) {
    bindActiveDevice();
    if (std::ranges::find(renderPassCache_, desc.renderPass, &RenderPassEntry::handle) == renderPassCache_.end())
        Log::fatal("RgRenderPassCache", "Framebuffer must reference a render pass owned by this cache");
    for (const auto& entry : framebufferCache_)
        if (entry.desc == desc) return entry.handle;
    const auto handle = RHI_DEVICE->framebuffer_create(desc);
    if (!handle) Log::fatal("RgRenderPassCache", "Framebuffer creation failed or unsupported");
    framebufferCache_.push_back({desc, handle});
    return handle;
}

void RgRenderPassCache::invalidateFramebuffers(rhi::RID view) {
    auto* device = RHI_DEVICE;
    std::erase_if(framebufferCache_, [&](const FramebufferEntry& entry) {
        if (std::ranges::find(entry.desc.views, view) == entry.desc.views.end()) return false;
        if (device && device->uid() == deviceUid_) device->framebuffer_destroy(entry.handle);
        return true;
    });
}

void RgRenderPassCache::clearFramebuffers() {
    auto* device = RHI_DEVICE;
    if (device && device->uid() == deviceUid_)
        for (const auto& entry : framebufferCache_) device->framebuffer_destroy(entry.handle);
    framebufferCache_.clear();
}

void RgRenderPassCache::clear() {
    clearFramebuffers();
    auto* device = RHI_DEVICE;
    if (device && device->uid() == deviceUid_)
        for (const auto& entry : renderPassCache_) device->render_pass_destroy(entry.handle);
    renderPassCache_.clear();
    deviceUid_ = 0;
}

} // namespace engine
