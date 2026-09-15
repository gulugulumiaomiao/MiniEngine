#include "asset/base/GenericAsset.h"

#include "core/serialization/Transfer.h"

namespace engine {

bool GenericAsset::transfer(Transfer& archive) {
    return archive.transfer("data", data);
}

} // namespace engine
