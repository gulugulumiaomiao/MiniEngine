#pragma once

#include "rhi/api/CommandBuffer.h"
#include "rhi/api/PipelineDesc.h"

#include <cstdint>

namespace engine::rhi {

enum class FrameStatus {
    Ready,
    OutOfDate,
};

struct SwapchainDesc {
    std::uint32_t width{};
    std::uint32_t height{};
    bool vsync{true};
};

class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    [[nodiscard]] virtual FrameStatus beginFrame() = 0;
    [[nodiscard]] virtual FrameStatus endFrame() = 0;
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

    // The frame command buffer; valid between beginFrame() and endFrame(). Recording
    // starts inside beginFrame() and endFrame() seals and submits it to the device.
    [[nodiscard]] virtual ICommandBuffer& commandBuffer() = 0;
    [[nodiscard]] virtual TextureHandle currentTexture() const = 0;
    [[nodiscard]] virtual TextureViewHandle currentTextureView() const = 0;
    [[nodiscard]] virtual ResourceState currentTextureState() const = 0;
    [[nodiscard]] virtual PixelFormat format() const = 0;
    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    [[nodiscard]] virtual std::uint32_t frameIndex() const = 0;
    // Number of swapchain images; adjacent tooling sizes its per-image resources with this.
    [[nodiscard]] virtual std::uint32_t imageCount() const = 0;
};

} // namespace engine::rhi
