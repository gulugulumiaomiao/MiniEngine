#include "asset/exporter/AssetExportPipeline.h"

#include "asset/exporter/GenericAssetExporter.h"
#include "asset/exporter/MaterialAssetExporter.h"
#include "asset/exporter/SceneAssetExporter.h"
#include "core/logging/Log.h"
#include "render/material/Material.h"

namespace engine {

bool AssetExportPipeline::initialize() {
    shutdown();
    bool ok = true;
    ok = registry_.registerExporter(std::make_unique<MaterialAssetExporter>()) && ok;
    ok = registry_.registerExporter(std::make_unique<SceneAssetExporter>()) && ok;
    ok = registry_.registerExporter(std::make_unique<GenericAssetExporter>()) && ok;
    initialized_ = ok;
    return ok;
}

void AssetExportPipeline::shutdown() {
    registry_.clear();
    initialized_ = false;
}

bool AssetExportPipeline::exportAsset(const Asset& asset,
                                      const VirtualPath& targetPath,
                                      std::string& error) {
    const AssetExporter* exporter = registry_.find(asset.type());
    if (!exporter) {
        error = std::string{"No exporter registered for type "} + assetTypeName(asset.type());
        Log::error("AssetExportPipeline", "%s: %s", error.c_str(), targetPath.string().c_str());
        return false;
    }
    if (!exporter->supports(targetPath)) {
        error = "Unsupported export target: " + targetPath.string();
        Log::error("AssetExportPipeline", "%s", error.c_str());
        return false;
    }
    const AssetExportResult result = exporter->write(asset, targetPath);
    if (!result.success) {
        error = result.error;
        Log::error("AssetExportPipeline",
                   "Cannot export %s asset: %s",
                   assetTypeName(asset.type()),
                   error.c_str());
        return false;
    }
    return true;
}

bool AssetExportPipeline::saveMaterial(const Material& material,
                                       const VirtualPath& targetPath,
                                       std::string& error) {
    const std::unique_ptr<MaterialAsset> asset =
        exportMaterialToAsset(material, targetPath, error);
    if (!asset)
        return false;
    return exportAsset(*asset, targetPath, error);
}

} // namespace engine
