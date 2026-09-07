#pragma once

#include "rhi/api/CommandEncoder.h"
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

    [[nodiscard]] virtual IGraphicsCommandEncoder& encoder() = 0;
    [[nodiscard]] virtual TextureHandle currentTexture() const = 0;
    [[nodiscard]] virtual TextureViewHandle currentTextureView() const = 0;
    [[nodiscard]] virtual ResourceState currentTextureState() const = 0;
    [[nodiscard]] virtual TextureFormat format() const = 0;
    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    [[nodiscard]] virtual std::uint32_t frameIndex() const = 0;
};

} // namespace engine::rhi
