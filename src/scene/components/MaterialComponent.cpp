#include "scene/components/MaterialComponent.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool MaterialComponentAsset::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("materials", materials) &&
           archive.transfer("enabled", enabled) && archive.endObject();
}

} // namespace engine
