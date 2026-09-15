#include "asset/importer/AssetImportHelpers.h"

#include "asset/derived_data/AssetArtifact.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"

#include <algorithm>
#include <ranges>
#include <string>

namespace engine {

AssetImportResult writeAssetArtifact(const AssetImportContext& context,
                                     Asset& asset,
                                     AssetType type,
                                     std::vector<VirtualPath> dependencies) {
    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        const std::string error =
            std::string{"Cannot create artifact directory: "} +
            context.artifactPath.parent().string();
        Log::error(assetTypeName(type), "%s", error.c_str());
        return AssetImportResult::failed(type, error);
    }

    BinaryWriter writer;
    if (!asset.transfer(writer)) {
        const std::string error =
            std::string{"Cannot serialize "} + assetTypeName(type) +
            " Artifact: " + context.sourcePath.string();
        Log::error(assetTypeName(type), "%s", error.c_str());
        return AssetImportResult::failed(type, error);
    }

    const AssetArtifact artifact{
        1, context.meta.assetId, type, context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        const std::string error =
            std::string{"Cannot save "} + assetTypeName(type) +
            " Artifact: " + context.artifactPath.string();
        Log::error(assetTypeName(type), "%s", error.c_str());
        return AssetImportResult::failed(type, error);
    }

    std::ranges::sort(dependencies, {}, &VirtualPath::string);
    dependencies.erase(std::ranges::unique(dependencies, {}, &VirtualPath::string).begin(),
                       dependencies.end());

    return AssetImportResult::succeeded(type, context.artifactPath, std::move(dependencies));
}

} // namespace engine
