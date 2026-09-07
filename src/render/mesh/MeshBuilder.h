#pragma once

#include "render/mesh/Mesh.h"

#include <optional>

namespace engine {

struct MeshBuildResult {
    MeshDesc desc;
    MeshData data;
};

class MeshBuilder final {
  public:
    [[nodiscard]] static std::optional<MeshBuildResult>
    build(const MeshBuildRecipe& recipe);
    [[nodiscard]] static std::optional<MeshAsset>
    buildAsset(const MeshBuildRecipe& recipe);
};

} // namespace engine
