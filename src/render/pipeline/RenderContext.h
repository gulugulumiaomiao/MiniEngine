#pragma once

#include "render/scene/RenderScene.h"

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

private:
    Renderer& renderer_;
    const RenderScene& scene_;
};

} // namespace engine
