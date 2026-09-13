#pragma once

#include "render/pipeline/RenderPipeline.h"
#include "render/render_graph/RgTexturePool.h"
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

    void setPipeline(std::unique_ptr<IRenderPipeline> pipeline) {
        pipeline_ = std::move(pipeline);
    }

    // Non-owning; the overlay must outlive the Renderer or be reset before shutdown.
    void setOverlay(IFrameOverlay* overlay) { overlay_ = overlay; }

    [[nodiscard]] rhi::IDevice& device() { return *device_; }
    [[nodiscard]] rhi::ISwapchain& swapchain() { return *swapchain_; }
    [[nodiscard]] RenderTarget& currentForwardTarget() {
        return *forwardTargets_[swapchain_->frameIndex()];
    }
    [[nodiscard]] RgTexturePool& rgTexturePool() { return *rgTexturePool_; }
    [[nodiscard]] Window& window() { return window_; }
    [[nodiscard]] std::uint64_t frameSerial() const { return frameSerial_; }

private:
    void recreateSwapchain();

    Window& window_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    std::vector<std::unique_ptr<RenderTarget>> forwardTargets_;
    std::optional<RgTexturePool> rgTexturePool_;
    std::unique_ptr<IRenderPipeline> pipeline_;
    IFrameOverlay* overlay_{};
    std::uint64_t frameSerial_{};
};

} // namespace engine
