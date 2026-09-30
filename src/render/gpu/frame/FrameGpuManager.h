#pragma once

#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/renderer/DrawList.h"
#include "render/uniform/Ubo.h"
#include "render/uniform/UniformSet.h"
#include "render/uniform/UniformSetLayout.h"

#include <array>
#include <cstdint>
#include <span>

namespace engine {

namespace rhi {
class IDevice;
}

// Owns the per-frame scene resources: the scene uniform block, the object-transform SSBO, the
// per-frame instance table and the shared set-0 UniformSet that binds them (plus the shadow map).
//
// The set-0 layout is built from the fixed engine convention {0: uniform, 1: object SSBO,
// 2: instance SSBO, 3: shadow combined-image-sampler} via UniformSetLayout::acquire. Because acquire
// dedups on the (binding, type) ordered hash, this resolves to the very same layout instance a
// compiled program reflects for set 0 (the user shaders declare this convention), so the shared
// scene set is compatible with every program's pipeline layout.
class FrameGpuManager final : public Singleton<FrameGpuManager> {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;
    static constexpr std::uint32_t kMaxRenderObjects = 1024;
    static constexpr std::uint32_t kMaxInstances = 8192;

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    void upload(std::uint32_t frameIndex, const DrawList& drawList);
    void shutdown();

    // Rebinds the frame's shadow map view (scene binding 3); no-op when unchanged.
    void bindShadowMap(std::uint32_t frameIndex, rhi::RID view);
    [[nodiscard]] const Ref<UniformSet>& sceneUniformSet(std::uint32_t frameIndex) const;

    // Per-frame instance table management. Batches draw multiple instances per
    // drawIndexed call; the table maps each instance slot to an object row.
    void beginFrame(std::uint32_t frameIndex);
    // Reserves `count` contiguous instance slots and returns the first slot index.
    [[nodiscard]] std::uint32_t reserveInstanceRegion(std::uint32_t frameIndex, std::uint32_t count);
    // Uploads object rows into the reserved region starting at `baseSlot`.
    void uploadInstanceRegion(std::uint32_t frameIndex,
                              std::uint32_t baseSlot,
                              std::span<const std::uint32_t> objectRows);

    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    struct FrameResources {
        Ref<UniformSet> sceneSet; // set 0: scene UBO(0) + object SSBO(1) + instance SSBO(2) + shadow(3)
        rhi::RID objectBuffer;
        rhi::RID instanceTable;
        rhi::RID shadowView;
        std::uint32_t instancesUsed{};
    };

    friend class Singleton<FrameGpuManager>;
    FrameGpuManager() = default;

    rhi::IDevice* device_{};
    Ref<UniformSetLayout> sceneLayout_; // shared set-0 layout (deduped against reflected set 0)
    Ref<UboLayout> sceneUboLayout_;     // synthetic scene block layout (sizeof(SceneDrawData))
    rhi::RID shadowSampler_;
    rhi::RID placeholderTexture_;
    rhi::RID placeholderView_;
    std::array<FrameResources, kFramesInFlight> frames_{};
};

} // namespace engine

#define FRAME_GPU_MANAGER (::engine::FrameGpuManager::instance())
