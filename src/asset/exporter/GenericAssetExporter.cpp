#include "asset/exporter/GenericAssetExporter.h"

#include "asset/base/GenericAsset.h"
#include "core/filesystem/FileSystem.h"

namespace engine {

bool GenericAssetExporter::supports(const VirtualPath& targetPath) const {
    return targetPath.valid() && targetPath.extension() != ".meta";
}

AssetExportResult GenericAssetExporter::write(const Asset& asset,
                                              const VirtualPath& targetPath) const {
    const auto* generic = dynamic_cast<const GenericAsset*>(&asset);
    if (!generic)
        return AssetExportResult::failed(AssetType::Generic, "Asset is not a GenericAsset");
    if (!FILE_SYSTEM.writeBinaryAtomic(targetPath, generic->data))
        return AssetExportResult::failed(AssetType::Generic,
                                         "Cannot write " + targetPath.string());
    return AssetExportResult::succeeded(AssetType::Generic, targetPath);
}

} // namespace engine
