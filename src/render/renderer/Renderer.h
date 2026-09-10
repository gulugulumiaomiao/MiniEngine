#pragma once

#include "render/renderer/DrawList.h"
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

    [[nodiscard]] rhi::IDevice& device() { return *device_; }

private:
    void recordDrawCommands(rhi::BindGroupHandle sceneBindGroup, const DrawList& drawList);
    void submitDrawList(DrawList drawList);
    void recreateSwapchain();

    Window& window_;
    std::unique_ptr<rhi::IDevice> device_;
    std::unique_ptr<rhi::ISwapchain> swapchain_;
    std::vector<std::unique_ptr<RenderTarget>> forwardTargets_;
    std::uint64_t frameSerial_{};
};

} // namespace engine
