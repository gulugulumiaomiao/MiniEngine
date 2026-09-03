#include "asset/importer/IAssetImporter.h"

#include <utility>

namespace engine {

AssetImportResult AssetImportResult::failed(AssetType type,
                                            std::string error) {
    AssetImportResult result;
    result.type = type;
    result.error = std::move(error);
    return result;
}

AssetImportResult AssetImportResult::succeeded(
    AssetType type, VirtualPath artifactPath,
    std::vector<VirtualPath> dependencies) {
    AssetImportResult result;
    result.success = true;
    result.type = type;
    result.artifactPath = std::move(artifactPath);
    result.dependencies = std::move(dependencies);
    return result;
}

} // namespace engine
