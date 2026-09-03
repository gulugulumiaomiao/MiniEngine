#include "asset/importer/MaterialAssetImporter.h"

#include "asset/AssetArtifact.h"
#include "core/Log.h"
#include "core/io/FileSystem.h"
#include "renderer/Shader.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace engine {

AssetImportResult MaterialAssetImporter::import(
    const AssetImportContext& context) const {
    const auto fail = [](std::string error) {
        Log::error("MaterialAssetImporter", "%s", error.c_str());
        return AssetImportResult::failed(AssetType::Material,
                                         std::move(error));
    };
    if (context.meta.assetType != AssetType::Material ||
        !context.meta.assetId.valid() || !context.sourcePath.valid() ||
        !context.artifactPath.valid()) {
        return fail("Invalid Material import context");
    }
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return fail("Cannot read MaterialAsset: " +
                    context.sourcePath.string());
    }
    if (!detail::parseMaterialAsset(context.sourcePath, *source)) {
        return fail("Cannot parse MaterialAsset: " +
                    context.sourcePath.string());
    }
    const nlohmann::json root = nlohmann::json::parse(*source, nullptr, false);
    if (root.is_discarded() ||
        !FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return fail("Cannot prepare Material Artifact: " +
                    context.artifactPath.string());
    }
    const AssetArtifact artifact{1, context.meta.assetId, AssetType::Material,
                                 context.sourcePath, root.dump()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return fail("Cannot save Material Artifact: " +
                    context.artifactPath.string());
    }
    return AssetImportResult::succeeded(AssetType::Material,
                                        context.artifactPath);
}

} // namespace engine
