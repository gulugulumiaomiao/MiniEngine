#include "asset/Asset.h"

namespace engine {

AssetType assetTypeFromName(std::string_view name) {
    if (name == "Shader") {
        return AssetType::Shader;
    }
    if (name == "Material") {
        return AssetType::Material;
    }
    return AssetType::Unknown;
}

} // namespace engine
