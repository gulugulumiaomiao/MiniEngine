#include "render/pipeline/RenderContext.h"

#include "core/logging/Log.h"
#include "render/renderer/Renderer.h"
#include "render/render_target/RenderTarget.h"
#include "rhi/api/Device.h"

namespace engine {

bool RenderContext::offscreenScene() const {
    return requireRenderer().offscreenScene();
}
std::uint32_t RenderContext::sceneWidth() const {
    return requireRenderer().sceneWidth();
}
std::uint32_t RenderContext::sceneHeight() const {
    return requireRenderer().sceneHeight();
}
rhi::PixelFormat RenderContext::sceneColorFormat() const {
    return offscreenScene() ? currentForwardTarget().colorFormat(0) : swapchain().format();
}

RenderContext::RenderContext(Renderer& renderer, const RenderScene& scene)
    : renderer_(&renderer), scene_(scene), texturePool_(renderer.rgTexturePool()),
      renderPassCache_(renderer.rgRenderPassCache()) {}

RenderContext::RenderContext(RgTexturePool& texturePool, RgRenderPassCache& renderPassCache,
                             const RenderScene& scene)
    : scene_(scene), texturePool_(texturePool), renderPassCache_(renderPassCache) {}

Renderer& RenderContext::requireRenderer() const {
    if (!renderer_) Log::fatal("RenderContext", "This context has no renderer");
    return *renderer_;
}

rhi::ISwapchain& RenderContext::swapchain() const {
    return requireRenderer().swapchain();
}

rhi::RID RenderContext::commandBuffer() const {
    return swapchain().commandBuffer();
}

RenderTarget& RenderContext::currentForwardTarget() const {
    return requireRenderer().currentForwardTarget();
}

RgTexturePool& RenderContext::rgTexturePool() const {
    return texturePool_;
}

RgRenderPassCache& RenderContext::rgRenderPassCache() const {
    return renderPassCache_;
}

std::uint32_t RenderContext::frameIndex() const {
    return swapchain().frameIndex();
}

std::uint64_t RenderContext::frameSerial() const {
    return requireRenderer().frameSerial();
}

} // namespace engine
