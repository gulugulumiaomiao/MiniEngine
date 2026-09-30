#pragma once

#include "core/base/Ref.h"
#include "render/uniform/UniformSet.h"
#include "render/uniform/UniformSetLayout.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <vector>

namespace engine {

// A resident material's GPU binding: its set-1 UniformSet (material UBO + texture bindings) plus
// the bookkeeping the LRU cache uses to decide between a cache hit, a uniform/texture refresh and
// a full rebuild.
struct MaterialStorageEntry {
    Ref<UniformSet> materialSet;
    Ref<UniformSetLayout> setLayout; // the set-1 layout the materialSet was built from

    std::uint64_t uniformVersion{}; // Material::version() of the last uniform write
    std::uint64_t ownerKey{};       // cache key of the material owning this slot
    std::uint64_t lastUsed{};       // LRU stamp, bumped on every acquire
    bool pendingRelease{};          // evicted slot: its resources are stale and must be rebuilt
    std::vector<rhi::TextureBinding> textureBindings; // texture signature bound into the set
};

} // namespace engine
