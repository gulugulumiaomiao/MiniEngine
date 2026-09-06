#include "scene/components/MeshComponent.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool MeshComponentAsset::transfer(Transfer& archive) {
    return archive.transfer("mesh", mesh) &&
           archive.transfer("enabled", enabled) &&
           archive.transfer("visible", visible) &&
           archive.transfer("cast_shadow", castShadow) &&
           archive.transfer("receive_shadow", receiveShadow) &&
           archive.transfer("layer_mask", layerMask);
}

} // namespace engine
