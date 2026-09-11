#include "render/pipeline/RenderContext.h"

#include "render/renderer/Renderer.h"
#include "render/render_target/RenderTarget.h"
#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"

namespace engine {

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
    return renderer_.currentForwardTarget();
}

std::uint32_t RenderContext::frameIndex() const {
    return renderer_.swapchain().frameIndex();
}

std::uint64_t RenderContext::frameSerial() const {
    return renderer_.frameSerial();
}

} // namespace engine
