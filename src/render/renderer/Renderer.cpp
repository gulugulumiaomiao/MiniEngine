#include "render/renderer/Renderer.h"

#include "core/logging/Log.h"
#include "render/pipeline/MiniForwardPipeline.h"
#include "render/pipeline/RenderContext.h"
#include "render/render_target/RenderTarget.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "runtime/window/Window.h"

namespace engine {

Renderer::Renderer(Window& window, rhi::Context context)
    : window_(window), device_(std::move(context.device)),
      swapchain_(std::move(context.swapchain)) {
    if (!device_ || !swapchain_) {
        Log::fatal("Renderer", "RHI context is incomplete");
    }
    rgTexturePool_.emplace(*device_);
    forwardTargets_.reserve(FrameGpuManager::kFramesInFlight);
    for (std::uint32_t frame = 0; frame < FrameGpuManager::kFramesInFlight; ++frame) {
        auto target = std::make_unique<RenderTarget>(*device_);
        RenderTargetDesc targetDesc;
        targetDesc.width = swapchain_->width();
        targetDesc.height = swapchain_->height();
        targetDesc.depthAttachment.emplace();
        targetDesc.depthAttachment->storeOp = rhi::StoreOp::DontCare;
        targetDesc.debugName = "ForwardTarget" + std::to_string(frame);
        if (!target->create(std::move(targetDesc))) {
            Log::fatal("Renderer", "Cannot create the Forward render target");
        }
        forwardTargets_.push_back(std::move(target));
    }
}

Renderer::~Renderer() {
    if (!device_)
        return;
    device_->waitIdle();
    forwardTargets_.clear();
    swapchain_.reset();
    device_.reset();
}

void Renderer::renderFrame(const RenderScene& scene) {
    if (!pipeline_) {
        Log::fatal("Renderer", "No render pipeline is bound");
    }
    GRAPHICS_PIPELINE_MANAGER.refreshShaders(frameSerial_,
                                             frameSerial_ + FrameGpuManager::kFramesInFlight);
    rgTexturePool_->beginFrame(swapchain_->frameIndex());
    if (swapchain_->beginFrame() == rhi::FrameStatus::OutOfDate) {
        recreateSwapchain();
        return;
    }
    GRAPHICS_PIPELINE_MANAGER.collect(frameSerial_);
    RenderContext context(*this, scene);
    pipeline_->render(context);
    const bool resized = window_.consumeResize();
    const rhi::FrameStatus status = swapchain_->endFrame();
    ++frameSerial_;
    if (status == rhi::FrameStatus::OutOfDate || resized)
        recreateSwapchain();
}

void Renderer::recreateSwapchain() {
    window_.waitForUsableFramebuffer();
    if (window_.shouldClose())
        return;
    const auto [width, height] = window_.framebufferSize();
    device_->waitIdle();
    GRAPHICS_PIPELINE_MANAGER.clear();
    swapchain_->resize(width, height);
    for (const std::unique_ptr<RenderTarget>& target : forwardTargets_) {
        if (!target->resize(swapchain_->width(), swapchain_->height())) {
            Log::fatal("Renderer", "Cannot resize the Forward render target");
        }
    }
    if (pipeline_) {
        pipeline_->onSwapchainChanged();
    }
}

void Renderer::waitIdle() {
    device_->waitIdle();
}

} // namespace engine
