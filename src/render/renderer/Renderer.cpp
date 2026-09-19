#include "render/renderer/Renderer.h"

#include "core/logging/Log.h"
#include "render/pipeline/MiniForwardPipeline.h"
#include "render/pipeline/RenderContext.h"
#include "render/render_target/RenderTarget.h"
#include "render/render_target/RenderTargetPool.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "rhi/api/CommandEncoder.h"
#include "runtime/window/Window.h"

#include <span>

namespace engine {

Renderer::Renderer(Window& window, rhi::Context context)
    : window_(window), device_(std::move(context.device)),
      swapchain_(std::move(context.swapchain)) {
    if (!device_ || !swapchain_) {
        Log::fatal("Renderer", "RHI context is incomplete");
    }
    renderTargetPool_.emplace(*device_, FrameGpuManager::kFramesInFlight);
    rgTexturePool_.emplace(*device_);
    forwardTargets_.reserve(FrameGpuManager::kFramesInFlight);
    for (std::uint32_t frame = 0; frame < FrameGpuManager::kFramesInFlight; ++frame) {
        RenderTargetDesc targetDesc;
        targetDesc.width = swapchain_->width();
        targetDesc.height = swapchain_->height();
        targetDesc.depthAttachment.emplace();
        targetDesc.depthAttachment->storeOp = rhi::StoreOp::DontCare;
        targetDesc.debugName = "ForwardTarget" + std::to_string(frame);
        const RenderTargetHandle target = renderTargetPool_->acquire(std::move(targetDesc));
        if (!target) {
            Log::fatal("Renderer", "Cannot create the Forward render target");
        }
        forwardTargets_.push_back(target);
    }
}

Renderer::~Renderer() {
    if (!device_)
        return;
    device_->waitIdle();
    pipeline_.reset();
    // Transient pool entries call into the device on destruction; the implicit member
    // destruction order would run after device_.reset() below and hit a dangling device.
    rgTexturePool_.reset();
    forwardTargets_.clear();
    renderTargetPool_.reset();
    swapchain_.reset();
    device_.reset();
}

void Renderer::renderFrame(const RenderScene& scene) {
    if (!pipeline_) {
        Log::fatal("Renderer", "No render pipeline is bound");
    }
    GRAPHICS_PIPELINE_MANAGER.refreshShaders(frameSerial_,
                                             frameSerial_ + FrameGpuManager::kFramesInFlight);
    if (swapchain_->beginFrame() == rhi::FrameStatus::OutOfDate) {
        recreateSwapchain();
        return;
    }
    rgTexturePool_->beginFrame(swapchain_->frameIndex());
    renderTargetPool_->beginFrame(swapchain_->frameIndex());
    GRAPHICS_PIPELINE_MANAGER.collect(frameSerial_);
    RenderContext context(*this, scene);
    if (context.sceneWidth() != 0 && context.sceneHeight() != 0) {
        if (!scene.camera() || !scene.camera()->target) {
            prepareForwardTarget();
        }
        if (!pipeline_->render(context)) {
            Log::error("Renderer", "Active render pipeline failed; using MiniForwardPipeline");
            MiniForwardPipeline fallback;
            if (!fallback.render(context)) {
                Log::fatal("Renderer", "Default render pipeline setup failed");
            }
        }
    }
    if (overlay_) {
        overlay_->recordOverlay(context);
    } else if (!context.backBufferWritten()) {
        // No overlay to guarantee a well-formed backbuffer: when the pipeline produced
        // no draw items the acquired swapchain image was never transitioned and is
        // still in the undefined layout. Presenting it unmodified trips the validation
        // layer, so clear it into PRESENT_SRC ourselves.
        rhi::IGraphicsCommandEncoder& encoder = swapchain_->encoder();
        const rhi::TextureHandle texture = swapchain_->currentTexture();
        const rhi::TextureBarrier toAttachment{
            .texture = texture,
            .before = rhi::ResourceState::Undefined,
            .after = rhi::ResourceState::ColorAttachment,
        };
        encoder.resourceBarriers(std::span{&toAttachment, 1});
        encoder.beginRendering({
            .renderArea = {.width = swapchain_->width(), .height = swapchain_->height()},
            .colorAttachments = {{
                .view = swapchain_->currentTextureView(),
                .loadOp = rhi::LoadOp::Clear,
            }},
        });
        encoder.endRendering();
        const rhi::TextureBarrier toPresent{
            .texture = texture,
            .before = rhi::ResourceState::ColorAttachment,
            .after = rhi::ResourceState::Present,
        };
        encoder.resourceBarriers(std::span{&toPresent, 1});
    }
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
    for (RenderTargetHandle& handle : forwardTargets_) {
        if (offscreenScene_)
            continue;
        RenderTarget* target = renderTargetPool_->find(handle);
        RenderTargetDesc desc = target->desc();
        desc.width = swapchain_->width();
        desc.height = swapchain_->height();
        const RenderTargetHandle replacement = renderTargetPool_->acquire(std::move(desc));
        if (!replacement) {
            Log::fatal("Renderer", "Cannot resize the Forward render target");
        }
        renderTargetPool_->release(handle);
        handle = replacement;
    }
    if (pipeline_) {
        pipeline_->onSwapchainChanged();
    }
    if (overlay_)
        overlay_->onSwapchainRecreated(*this);
}

void Renderer::waitIdle() {
    device_->waitIdle();
}

RenderTarget& Renderer::currentForwardTarget() {
    RenderTarget* target = renderTargetPool_->find(forwardTargets_[swapchain_->frameIndex()]);
    if (!target) {
        Log::fatal("Renderer", "Current Forward render target is unavailable");
    }
    return *target;
}

void Renderer::prepareForwardTarget() {
    // beginFrame waited for this slot's fence. Other slots retain their images until
    // their own fence completes, including during interactive viewport resizing.
    RenderTarget& target = currentForwardTarget();
    const std::size_t colorCount = offscreenScene_ ? 1U : 0U;
    if (target.width() == sceneWidth() && target.height() == sceneHeight() &&
        target.colorAttachmentCount() == colorCount &&
        (!offscreenScene_ || target.colorFormat(0) == swapchain_->format()))
        return;
    RenderTargetDesc desc;
    desc.width = sceneWidth();
    desc.height = sceneHeight();
    desc.depthAttachment.emplace();
    desc.depthAttachment->storeOp = rhi::StoreOp::DontCare;
    desc.debugName = "ForwardTarget" + std::to_string(swapchain_->frameIndex());
    if (offscreenScene_) {
        desc.colorAttachments.push_back({
            .format = swapchain_->format(),
            .additionalUsage = rhi::TextureUsage::Sampled,
        });
    }
    const RenderTargetHandle replacement = renderTargetPool_->acquire(std::move(desc));
    if (!replacement)
        Log::fatal("Renderer", "Cannot prepare the scene render target");
    RenderTargetHandle& current = forwardTargets_[swapchain_->frameIndex()];
    renderTargetPool_->release(current);
    current = replacement;
}

} // namespace engine
