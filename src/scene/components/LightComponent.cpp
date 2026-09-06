#include "scene/components/LightComponent.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool LightComponentAsset::transfer(Transfer& archive) {
    return archive.transfer("light_type", type) &&
           archive.transfer("color", color) &&
           archive.transfer("intensity", intensity) &&
           archive.transfer("range", range) &&
           archive.transfer("inner_spot_angle", innerSpotAngle) &&
           archive.transfer("outer_spot_angle", outerSpotAngle) &&
           archive.transfer("cast_shadow", castShadow) &&
           archive.transfer("culling_mask", cullingMask) &&
           archive.transfer("enabled", enabled);
}

} // namespace engine
