#pragma once

#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/uniform/Ubo.h"
#include "render/uniform/UniformSet.h"
#include "render/uniform/UniformSetLayout.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// GPU-side manager for the shared global uniform descriptor set (set 2).
//
// The set-2 layout follows the reflected convention: an optional uniform block at binding 0 (only
// when there are non-texture globals) plus combined-image-samplers at bindings 1..M, where M is the
// current global-texture count. It is built via UniformSetLayout::acquire so it dedups to the same
// instance programs reflect for set 2. When there are no globals at all the manager hands back an
// empty set and the renderer skips binding set 2 (matching the generator, which emits no set-2
// declarations when there are no globals).
class GlobalUniformGpuManager final : public Singleton<GlobalUniformGpuManager> {
public:
    static constexpr std::uint32_t kMaxGlobalTextures = 8;

    ~GlobalUniformGpuManager();

    [[nodiscard]] bool initialize(std::uint32_t frameCount);
    void beginFrame(std::uint32_t frameIndex);
    void shutdown();

    [[nodiscard]] const Ref<UniformSet>& resolve(std::uint32_t frameIndex) const;
    [[nodiscard]] bool initialized() const { return initialized_; }

private:
    friend class Singleton<GlobalUniformGpuManager>;
    GlobalUniformGpuManager();

    struct FrameResources {
        Ref<UniformSet> set;
    };

    // (Re)builds the shared set-2 layout + UBO layout when the global property set changes; resets
    // the per-frame sets so they are rebuilt against the new layout.
    void ensureLayout();
    void updateFrame(std::uint32_t frameIndex);

    bool initialized_{};
    Ref<UniformSetLayout> layout_;
    Ref<UboLayout> uboLayout_;
    bool layoutHasUbo_{};
    std::uint32_t layoutTextureCount_{};
    std::uint64_t layoutUboSize_{};
    std::vector<FrameResources> frames_;
};

} // namespace engine

#define GLOBAL_UNIFORM_GPU_MANAGER (::engine::GlobalUniformGpuManager::instance())
