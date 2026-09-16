#include "asset/exporter/SceneAssetExporter.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/SceneAssetFormat.h"
#include "core/filesystem/FileSystem.h"
#include "scene/scene/SceneAsset.h"

namespace engine {

bool SceneAssetExporter::supports(const VirtualPath& targetPath) const {
    return targetPath.valid() && isAssetScheme(targetPath.scheme()) &&
           targetPath.relativePath().ends_with(".scene.json");
}

AssetExportResult SceneAssetExporter::write(const Asset& asset,
                                            const VirtualPath& targetPath) const {
    const auto* scene = dynamic_cast<const SceneAsset*>(&asset);
    if (!scene)
        return AssetExportResult::failed(AssetType::Scene, "Asset is not a SceneAsset");
    const std::string json = format::writeSceneAssetJson(*scene, ASSET_DATABASE);
    if (!FILE_SYSTEM.writeTextAtomic(targetPath, json))
        return AssetExportResult::failed(AssetType::Scene, "Cannot write " + targetPath.string());
    return AssetExportResult::succeeded(AssetType::Scene, targetPath);
}

} // namespace engine
