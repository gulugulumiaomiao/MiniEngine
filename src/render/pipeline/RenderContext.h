#pragma once

#include "render/scene/RenderScene.h"
#include "rhi/api/PipelineDesc.h"

#include <cstdint>

namespace engine {

class Renderer;
class RenderTarget;
class RgTexturePool;

namespace rhi {
class IDevice;
class ISwapchain;
class IGraphicsCommandEncoder;
} // namespace rhi

class RenderContext final {
public:
    RenderContext(Renderer& renderer, const RenderScene& scene);

    [[nodiscard]] rhi::IDevice& device() const;
    [[nodiscard]] rhi::ISwapchain& swapchain() const;
    [[nodiscard]] rhi::IGraphicsCommandEncoder& encoder() const;
    [[nodiscard]] RenderTarget& currentForwardTarget() const;
    [[nodiscard]] RgTexturePool& rgTexturePool() const;
    [[nodiscard]] std::uint32_t frameIndex() const;
    [[nodiscard]] std::uint64_t frameSerial() const;
    [[nodiscard]] const RenderScene& scene() const { return scene_; }
    [[nodiscard]] bool offscreenScene() const;
    [[nodiscard]] std::uint32_t sceneWidth() const;
    [[nodiscard]] std::uint32_t sceneHeight() const;
    [[nodiscard]] rhi::TextureFormat sceneColorFormat() const;

    // True once a pass actually rendered into the swapchain backbuffer this frame.
    // The overlay uses this instead of guessing from scene contents: a scene can have
    // objects yet still produce no draw items, leaving the backbuffer untouched.
    [[nodiscard]] bool backBufferWritten() const { return backBufferWritten_; }
    void markBackBufferWritten() { backBufferWritten_ = true; }

private:
    Renderer& renderer_;
    const RenderScene& scene_;
    bool backBufferWritten_{};
};

} // namespace engine
