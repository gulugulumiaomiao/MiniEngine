#pragma once

#include "core/base/Ref.h"
#include "render/mesh/MeshTypes.h"

#include <optional>

namespace engine {

class MeshBuilder final {
public:
    [[nodiscard]] static std::optional<MeshCreateInfo> build(const MeshBuildRecipe& recipe);
};

} // namespace engine
