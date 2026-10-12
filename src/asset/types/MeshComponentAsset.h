#pragma once
#include "asset/types/MeshAssetData.h"
#include "core/filesystem/VirtualPath.h"
namespace engine {
enum class MeshComponentAssetSourceType { Asset, Primitive };
struct MeshComponentAsset final : public Transferable {
    MeshComponentAssetSourceType sourceType{MeshComponentAssetSourceType::Asset};
    VirtualPath mesh;
    MeshBuildRecipeAssetData primitiveRecipe;
    bool enabled{true};
    bool visible{true};
    bool castShadow{true};
    bool receiveShadow{true};
    std::uint32_t layerMask{1};

    bool operator==(const MeshComponentAsset& other) const {
        return sourceType == other.sourceType && mesh == other.mesh &&
               primitiveRecipe == other.primitiveRecipe && enabled == other.enabled &&
               visible == other.visible && castShadow == other.castShadow &&
               receiveShadow == other.receiveShadow && layerMask == other.layerMask;
    }
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

} // namespace engine
