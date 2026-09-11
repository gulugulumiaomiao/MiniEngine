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

// Cache that keeps material GPU resources resident across frames. Slots are
// owned by the material that last claimed them; acquire() reuses an existing
// slot when the same material shows up again and evicts the least recently
// used one when the capacity is exhausted.
class MaterialBindingCache final {
public:
    [[nodiscard]] bool initialize(std::uint32_t frameCount, std::uint32_t capacity);
    void beginFrame(std::uint32_t frameIndex);
    [[nodiscard]] MaterialBindingCacheSlot acquire(std::uint64_t materialKey);
    [[nodiscard]] std::vector<MaterialGpuResource> extractAll();
    [[nodiscard]] std::size_t size() const;
    void reset();

private:
    struct Frame {
        std::vector<MaterialGpuResource> resources;
        std::unordered_map<std::uint64_t, std::uint32_t> lookup;
    };

    std::vector<Frame> frames_;
    std::uint32_t currentFrame_{};
    std::uint32_t capacity_{};
    std::uint64_t lruStamp_{};
};

} // namespace engine
