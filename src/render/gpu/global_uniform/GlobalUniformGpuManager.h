#pragma once

#include "core/base/Singleton.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// GPU-side manager for the shared global uniform descriptor set (set 2).
//
// The bind group layout is fixed: binding 0 is a uniform buffer and bindings 1..N
// are sampled textures. The actual uniform buffer size and texture bindings are
// updated each frame from GlobalUniformManager. Double buffering keeps the in-flight
// frame from observing CPU writes intended for the next frame.
class GlobalUniformGpuManager final : public Singleton<GlobalUniformGpuManager> {
public:
    static constexpr std::uint32_t kMaxGlobalTextures = 8;

    ~GlobalUniformGpuManager();

    [[nodiscard]] bool initialize(rhi::IDevice& device, std::uint32_t frameCount);
    void beginFrame(std::uint32_t frameIndex);
    void shutdown();

    [[nodiscard]] rhi::RID resolve(std::uint32_t frameIndex) const;
    [[nodiscard]] rhi::RID bindGroupLayout() const { return layout_; }
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    friend class Singleton<GlobalUniformGpuManager>;
    GlobalUniformGpuManager();

    struct FrameResources {
        rhi::RID uniformBuffer;
        rhi::RID bindGroup;
        std::uint64_t uniformVersion{};
        std::uint64_t uniformSize{};
        std::vector<rhi::TextureBinding> textureBindings;
    };

    void updateFrame(std::uint32_t frameIndex);

    rhi::IDevice* device_{};
    rhi::RID layout_;
    std::vector<FrameResources> frames_;
    std::uint64_t lastLayoutVersion_{};
};

} // namespace engine

#define GLOBAL_UNIFORM_GPU_MANAGER (::engine::GlobalUniformGpuManager::instance())
