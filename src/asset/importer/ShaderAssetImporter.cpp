#include "asset/importer/ShaderAssetImporter.h"

#include "asset/derived_data/AssetArtifact.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "render/shader/Shader.h"
#include <utility>

namespace engine {
namespace {

[[nodiscard]] AssetImportResult failImport(std::string error) {
    Log::error("ShaderAssetImporter", "%s", error.c_str());
    return AssetImportResult::failed(AssetType::Shader, std::move(error));
}

} // namespace

AssetImportResult ShaderAssetImporter::import(const AssetImportContext& context) const {
    if (context.meta.assetType != AssetType::Shader || !context.meta.assetId.valid() ||
        !context.sourcePath.valid() || !context.artifactPath.valid()) {
        return failImport("Invalid Shader import context");
    }
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return failImport("Cannot read ShaderAsset: " + context.sourcePath.string());
    }
    const std::shared_ptr<ShaderAsset> shader =
        detail::parseShaderAsset(context.sourcePath, *source);
    if (!shader) {
        return failImport("Cannot parse ShaderAsset: " + context.sourcePath.string());
    }

    if (!FILE_SYSTEM.createDirectories(context.artifactPath.parent())) {
        return failImport("Cannot create Shader Artifact directory: " +
                          context.artifactPath.parent().string());
    }
    BinaryWriter writer;
    if (!shader->transfer(writer)) {
        return failImport("Cannot serialize Shader Artifact: " + context.sourcePath.string());
    }
    const AssetArtifact artifact{
        1, context.meta.assetId, AssetType::Shader, context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact)) {
        return failImport("Cannot save Shader Artifact: " + context.artifactPath.string());
    }
    return AssetImportResult::succeeded(AssetType::Shader, context.artifactPath);
}

} // namespace engine
