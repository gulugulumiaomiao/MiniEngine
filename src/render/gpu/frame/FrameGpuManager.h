#pragma once

#include "core/base/Singleton.h"
#include "render/renderer/DrawList.h"

#include <array>
#include <cstdint>

namespace engine {

namespace rhi {
class IDevice;
}

class FrameGpuManager final : public Singleton<FrameGpuManager> {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;
    static constexpr std::uint32_t kMaxRenderObjects = 1024;

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] rhi::BindGroupHandle upload(std::uint32_t frameIndex, const DrawList& drawList);
    void shutdown();

    [[nodiscard]] rhi::BindGroupLayoutHandle sceneLayout() const { return sceneLayout_; }
    [[nodiscard]] rhi::BindGroupLayoutHandle materialLayout() const { return materialLayout_; }
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    struct FrameResources {
        rhi::BufferHandle sceneBuffer;
        rhi::BufferHandle objectBuffer;
        rhi::BindGroupHandle sceneBindGroup;
    };

    friend class Singleton<FrameGpuManager>;
    FrameGpuManager() = default;

    rhi::IDevice* device_{};
    rhi::BindGroupLayoutHandle sceneLayout_;
    rhi::BindGroupLayoutHandle materialLayout_;
    std::array<FrameResources, kFramesInFlight> frames_{};
};

} // namespace engine

#define FRAME_GPU_MANAGER (::engine::FrameGpuManager::instance())
