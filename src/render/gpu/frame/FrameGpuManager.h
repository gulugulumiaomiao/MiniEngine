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
    void upload(std::uint32_t frameIndex, const DrawList& drawList);
    void shutdown();

    // Binds the frame's shadow map view to scene binding 3; rebuilds only on view change.
    void bindShadowMap(std::uint32_t frameIndex, rhi::RID view);
    [[nodiscard]] rhi::RID sceneBindGroup(std::uint32_t frameIndex) const;

    // Per-frame instance table management. Batches draw multiple instances per
    // drawIndexed call; the table maps each instance slot to an object row.
    void beginFrame(std::uint32_t frameIndex);
    // Reserves `count` contiguous instance slots and returns the first slot index.
    [[nodiscard]] std::uint32_t reserveInstanceRegion(std::uint32_t frameIndex, std::uint32_t count);
    // Uploads object rows into the reserved region starting at `baseSlot`.
    void uploadInstanceRegion(std::uint32_t frameIndex,
                              std::uint32_t baseSlot,
                              std::span<const std::uint32_t> objectRows);

    [[nodiscard]] rhi::RID sceneLayout() const { return sceneLayout_; }
    [[nodiscard]] rhi::RID materialLayout() const { return materialLayout_; }
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    struct FrameResources {
        rhi::RID sceneBuffer;
        rhi::RID objectBuffer;
        rhi::RID instanceTable;
        rhi::RID sceneBindGroup;
        rhi::RID shadowView;
        std::uint32_t instancesUsed{};
    };

    friend class Singleton<FrameGpuManager>;
    FrameGpuManager() = default;

    rhi::IDevice* device_{};
    rhi::RID sceneLayout_;
    rhi::RID materialLayout_;
    rhi::RID shadowSampler_;
    rhi::RID placeholderTexture_;
    rhi::RID placeholderView_;
    std::array<FrameResources, kFramesInFlight> frames_{};
};

} // namespace engine

#define FRAME_GPU_MANAGER (::engine::FrameGpuManager::instance())
