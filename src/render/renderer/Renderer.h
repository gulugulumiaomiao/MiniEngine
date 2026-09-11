#pragma once

#include "render/pipeline/RenderPipeline.h"
#include "rhi/RhiFactory.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

class RenderScene;
class RenderTarget;
class Window;

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

    [[nodiscard]] rhi::IDevice& device() { return *device_; }
    [[nodiscard]] rhi::ISwapchain& swapchain() { return *swapchain_; }
    [[nodiscard]] RenderTarget& currentForwardTarget() {
        return *forwardTargets_[swapchain_->frameIndex()];
    }
    [[nodiscard]] Window& window() { return window_; }
    [[nodiscard]] std::uint64_t frameSerial() const { return frameSerial_; }

private:
    void recreateSwapchain();

    Window& window_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    std::vector<std::unique_ptr<RenderTarget>> forwardTargets_;
    std::unique_ptr<IRenderPipeline> pipeline_;
    std::uint64_t frameSerial_{};
};

} // namespace engine
