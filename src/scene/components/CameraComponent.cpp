#include "scene/components/CameraComponent.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool CameraComponentAsset::transfer(Transfer& archive) {
    return archive.transfer("projection", projection) &&
           archive.transfer("field_of_view", fieldOfView) &&
           archive.transfer("orthographic_size", orthographicSize) &&
           archive.transfer("near_plane", nearPlane) &&
           archive.transfer("far_plane", farPlane) &&
           archive.transfer("clear_color", clearColor) &&
           archive.transfer("culling_mask", cullingMask) &&
           archive.transfer("priority", priority) &&
           archive.transfer("primary", primary) &&
           archive.transfer("enabled", enabled);
}

} // namespace engine
