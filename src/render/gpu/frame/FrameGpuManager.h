#pragma once

#include "core/base/Singleton.h"
#include "render/renderer/DrawList.h"

#include <array>
#include <cstdint>
#include <span>

namespace engine {

namespace rhi {
class IDevice;
}

class FrameGpuManager final : public Singleton<FrameGpuManager> {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;
    static constexpr std::uint32_t kMaxRenderObjects = 1024;
    static constexpr std::uint32_t kMaxInstances = 8192;

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] rhi::BindGroupHandle upload(std::uint32_t frameIndex, const DrawList& drawList);
    void shutdown();

    // Per-frame instance table management. Batches draw multiple instances per
    // drawIndexed call; the table maps each instance slot to an object row.
    void beginFrame(std::uint32_t frameIndex);
    // Reserves `count` contiguous instance slots and returns the first slot index.
    [[nodiscard]] std::uint32_t reserveInstanceRegion(std::uint32_t frameIndex, std::uint32_t count);
    // Uploads object rows into the reserved region starting at `baseSlot`.
    void uploadInstanceRegion(std::uint32_t frameIndex,
                              std::uint32_t baseSlot,
                              std::span<const std::uint32_t> objectRows);

    [[nodiscard]] rhi::BindGroupLayoutHandle sceneLayout() const { return sceneLayout_; }
    [[nodiscard]] rhi::BindGroupLayoutHandle materialLayout() const { return materialLayout_; }
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    struct FrameResources {
        rhi::BufferHandle sceneBuffer;
        rhi::BufferHandle objectBuffer;
        rhi::BufferHandle instanceTable;
        rhi::BindGroupHandle sceneBindGroup;
        std::uint32_t instancesUsed{};
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
