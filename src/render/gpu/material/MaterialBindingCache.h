#pragma once

#include "render/gpu/material/MaterialGpuResource.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

struct MaterialBindingCacheSlot {
    MaterialGpuResource* resource{};
    bool cacheHit{};
};

class MaterialBindingCache final {
public:
    [[nodiscard]] bool initialize(std::uint32_t frameCount);
    void beginFrame(std::uint32_t frameIndex);
    [[nodiscard]] MaterialBindingCacheSlot acquire(std::uint64_t materialKey);
    [[nodiscard]] std::vector<MaterialGpuResource> extractAll();
    [[nodiscard]] std::size_t size() const;
    void reset();

private:
    struct Frame {
        std::vector<MaterialGpuResource> resources;
        std::unordered_map<std::uint64_t, std::uint32_t> lookup;
        std::uint32_t used{};
    };

    std::vector<Frame> frames_;
    std::uint32_t currentFrame_{};
};

} // namespace engine
