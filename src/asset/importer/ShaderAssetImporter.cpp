#include "asset/importer/ShaderAssetImporter.h"

#include "asset/format/ShaderAssetFormat.h"
#include "asset/importer/AssetImportHelpers.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include <utility>

namespace engine {
namespace {

[[nodiscard]] AssetImportResult failImport(std::string error) {
    Log::error("ShaderAssetImporter", "%s", error.c_str());
    return AssetImportResult::failed(AssetType::Shader, std::move(error));
}

} // namespace

bool ShaderImportSettings::transfer(Transfer& archive) {
    (void)archive;
    return true;
}

Hash64 ShaderImportSettings::hash() const {
    return hashString("ShaderImportSettings");
}

std::unique_ptr<AssetImportSettings>
ShaderAssetImporter::createDefaultSettings(const VirtualPath&) const {
    return std::make_unique<ShaderImportSettings>();
}

std::vector<VirtualPath> ShaderAssetImporter::gatherDependencies(const AssetImportContext&,
                                                                 const AssetImportSettings&) const {
    return {};
}

AssetImportResult ShaderAssetImporter::import(const AssetImportContext& context,
                                              const AssetImportSettings&) const {
    if (context.meta.assetType != AssetType::Shader || !context.meta.assetId.valid() ||
        !context.sourcePath.valid() || !context.artifactPath.valid()) {
        return failImport("Invalid Shader import context");
    }
    const auto source = FILE_SYSTEM.readText(context.sourcePath);
    if (!source) {
        return failImport("Cannot read ShaderAsset: " + context.sourcePath.string());
    }
    const std::shared_ptr<ShaderAsset> shader =
        format::parseShaderAsset(context.sourcePath, *source);
    if (!shader) {
        return failImport("Cannot parse ShaderAsset: " + context.sourcePath.string());
    }

    return writeAssetArtifact(context, *shader, AssetType::Shader);
}

} // namespace engine
