#include "asset/base/Asset.h"

namespace engine {

AssetType assetTypeFromName(std::string_view name) {
    if (name == "Shader") {
        return AssetType::Shader;
    }
    if (name == "Material") {
        return AssetType::Material;
    }
    if (name == "Mesh") {
        return AssetType::Mesh;
    }
    if (name == "Texture") {
        return AssetType::Texture;
    }
    if (name == "Scene") {
        return AssetType::Scene;
    }
    return AssetType::Unknown;
}

} // namespace engine
