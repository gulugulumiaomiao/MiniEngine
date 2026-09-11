#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <vector>

namespace engine {

struct MaterialGpuResource {
    rhi::BufferHandle uniformBuffer;
    std::uint64_t uniformCapacity{};
    rhi::BindGroupHandle bindGroup;

    // Persistent-residency bookkeeping. The slot keeps its GPU resources across
    // frames; these fields let the manager decide between a full rebuild, a
    // uniform-only update, and a pure cache hit.
    std::uint64_t uniformVersion{};                 // Material::version() of the last upload
    std::uint64_t boundSize{};                      // byte size bound into bindGroup
    std::uint64_t ownerKey{};                       // cache key of the material owning this slot
    std::uint64_t lastUsed{};                       // LRU stamp, bumped on every acquire
    bool pendingRelease{};                          // evicted slot: manager must free handles first
    std::vector<rhi::TextureBinding> textureBindings; // texture signature bound into bindGroup
};

} // namespace engine
