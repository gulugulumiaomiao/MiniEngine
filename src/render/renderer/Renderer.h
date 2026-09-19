#pragma once

#include "render/pipeline/RenderPipeline.h"
#include "render/render_graph/RgTexturePool.h"
#include "render/render_target/RenderTargetPool.h"
#include "rhi/RhiFactory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace engine {

class RenderScene;
class RenderTarget;
class RenderContext;
class Renderer;
class Window;

// Records additional draw work into the current frame's command buffer after the render
// pipeline finished. Used by engine-adjacent tooling such as the editor UI overlay.
class IFrameOverlay {
public:
    virtual ~IFrameOverlay() = default;

    // The overlay is the last writer before the swapchain presents: it must leave the
    // acquired backbuffer in PRESENT_SRC. Whether it can load scene content is decided
    // via RenderContext::backBufferWritten(), which is set when a pass actually drew
    // into the backbuffer this frame.
    virtual void recordOverlay(RenderContext& context) = 0;
    // Called after the swapchain was recreated while the device is idle.
    virtual void onSwapchainRecreated(Renderer& renderer) = 0;
};

class Renderer final {
public:
    Renderer(Window& window, rhi::Context context);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void renderFrame(const RenderScene& scene);
    void waitIdle();

    // A zero extent suspends scene rendering while overlays continue to present.
    void setSceneViewport(std::uint32_t width, std::uint32_t height) {
        offscreenScene_ = true;
        sceneWidth_ = width;
        sceneHeight_ = height;
    }
    void resetSceneViewport() { offscreenScene_ = false; }
    [[nodiscard]] bool offscreenScene() const { return offscreenScene_; }
    [[nodiscard]] std::uint32_t sceneWidth() const { return offscreenScene_ ? sceneWidth_ : swapchain_->width(); }
    [[nodiscard]] std::uint32_t sceneHeight() const { return offscreenScene_ ? sceneHeight_ : swapchain_->height(); }
    [[nodiscard]] float sceneAspectRatio() const {
        return sceneHeight() != 0 ? static_cast<float>(sceneWidth()) / static_cast<float>(sceneHeight()) : 1.0F;
    }

    void setPipeline(std::unique_ptr<IRenderPipeline> pipeline) {
        pipeline_ = std::move(pipeline);
    }

    // Non-owning; the overlay must outlive the Renderer or be reset before shutdown.
    void setOverlay(IFrameOverlay* overlay) { overlay_ = overlay; }

    [[nodiscard]] rhi::IDevice& device() { return *device_; }
    [[nodiscard]] rhi::ISwapchain& swapchain() { return *swapchain_; }
    [[nodiscard]] RenderTarget& currentForwardTarget();
    [[nodiscard]] RenderTarget* renderTarget(RenderTargetHandle handle) {
        return renderTargetPool_->find(handle);
    }
    [[nodiscard]] RenderTargetPool& renderTargetPool() { return *renderTargetPool_; }
    [[nodiscard]] RgTexturePool& rgTexturePool() { return *rgTexturePool_; }
    [[nodiscard]] Window& window() { return window_; }
    [[nodiscard]] std::uint64_t frameSerial() const { return frameSerial_; }

private:
    void recreateSwapchain();
    void prepareForwardTarget();

    Window& window_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    std::vector<RenderTargetHandle> forwardTargets_;
    std::optional<RenderTargetPool> renderTargetPool_;
    std::optional<RgTexturePool> rgTexturePool_;
    std::unique_ptr<IRenderPipeline> pipeline_;
    IFrameOverlay* overlay_{};
    std::uint64_t frameSerial_{};
    bool offscreenScene_{};
    std::uint32_t sceneWidth_{};
    std::uint32_t sceneHeight_{};
};

} // namespace engine
