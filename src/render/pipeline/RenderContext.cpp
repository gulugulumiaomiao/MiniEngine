#include "render/pipeline/RenderContext.h"

#include "core/logging/Log.h"
#include "render/renderer/Renderer.h"
#include "render/renderer/RenderFrameStats.h"
#include "render/render_target/RenderTarget.h"
#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"

namespace engine {

bool RenderContext::offscreenScene() const {
    return (scene_.camera() && scene_.camera()->target) || renderer_.offscreenScene();
}
std::uint32_t RenderContext::sceneWidth() const {
    return scene_.camera() && scene_.camera()->target ? currentForwardTarget().width()
                                                     : renderer_.sceneWidth();
}
std::uint32_t RenderContext::sceneHeight() const {
    return scene_.camera() && scene_.camera()->target ? currentForwardTarget().height()
                                                     : renderer_.sceneHeight();
}
rhi::TextureFormat RenderContext::sceneColorFormat() const {
    return offscreenScene() ? currentForwardTarget().colorFormat(0) : swapchain().format();
}

RenderContext::RenderContext(Renderer& renderer, const RenderScene& scene)
    : renderer_(renderer), scene_(scene) {}

rhi::IDevice& RenderContext::device() const {
    return renderer_.device();
}

rhi::ISwapchain& RenderContext::swapchain() const {
    return renderer_.swapchain();
}

rhi::IGraphicsCommandEncoder& RenderContext::encoder() const {
    return renderer_.swapchain().encoder();
}

RenderTarget& RenderContext::currentForwardTarget() const {
    if (scene_.camera() && scene_.camera()->target) {
        RenderTarget* target = renderer_.renderTarget(*scene_.camera()->target);
        if (!target) {
            Log::fatal("RenderContext", "Camera render target is invalid or retired");
        }
        return *target;
    }
    return renderer_.currentForwardTarget();
}

RgTexturePool& RenderContext::rgTexturePool() const {
    return renderer_.rgTexturePool();
}

std::uint32_t RenderContext::frameIndex() const {
    return renderer_.swapchain().frameIndex();
}

std::uint64_t RenderContext::frameSerial() const {
    return renderer_.frameSerial();
}

RenderFrameStats& RenderContext::frameStats() const {
    return renderer_.frameStats();
}

void RenderContext::recordSubmission(const DrawSubmissionStats& submission) const {
    renderer_.frameStats().recordSubmission(submission);
}

} // namespace engine
