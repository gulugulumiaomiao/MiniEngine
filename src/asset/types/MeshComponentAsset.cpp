#include "asset/types/MeshComponentAsset.h"
#include "core/serialization/Transfer.h"
namespace engine {
bool MeshComponentAsset::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("source_type", sourceType))
        return false;
    if (sourceType == MeshComponentAssetSourceType::Asset) {
        if (!archive.transfer("mesh", mesh))
            return false;
    } else if (sourceType == MeshComponentAssetSourceType::Primitive) {
        if (!archive.transfer("primitive_recipe", primitiveRecipe))
            return false;
    } else {
        return false;
    }
    return archive.transfer("enabled", enabled) && archive.transfer("visible", visible) &&
           archive.transfer("cast_shadow", castShadow) &&
           archive.transfer("receive_shadow", receiveShadow) &&
           archive.transfer("layer_mask", layerMask) && archive.endObject();
}

} // namespace engine
