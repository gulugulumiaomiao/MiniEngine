#pragma once

#include "render/renderer/DrawList.h"

#include <cstdint>
#include <vector>

namespace engine {

struct MeshStorageSurface {
    std::uint32_t subMesh{};
    std::uint32_t materialSlot{};
};

struct MeshStorageLod {
    float minimumScreenCoverage{};
    std::vector<MeshStorageSurface> surfaces;
};

struct MeshStorageEntry {
    MeshDrawInfo drawInfo;
    // Layer-2 extension points. They deliberately contain no Vulkan types.
    std::vector<MeshStorageLod> lods;
    bool skinned{};
    bool hasBlendShapes{};
    bool keepCpuCopy{};
};

} // namespace engine
