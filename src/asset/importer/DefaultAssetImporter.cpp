#include "asset/importer/DefaultAssetImporter.h"

#include "asset/base/GenericAsset.h"
#include "asset/importer/AssetImportHelpers.h"
#include "core/filesystem/FileSystem.h"
#include "core/math/hash.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"

namespace engine {

bool GenericImportSettings::transfer(Transfer& archive) {
    (void)archive;
    return true;
}

Hash64 GenericImportSettings::hash() const {
    return hashString("GenericImportSettings");
}

bool DefaultAssetImporter::supports(const VirtualPath& sourcePath) const {
    return sourcePath.valid() && sourcePath.extension() != ".meta";
}

std::unique_ptr<AssetImportSettings>
DefaultAssetImporter::createDefaultSettings(const VirtualPath&) const {
    return std::make_unique<GenericImportSettings>();
}

std::vector<VirtualPath>
DefaultAssetImporter::gatherDependencies(const AssetImportContext&,
                                         const AssetImportSettings&) const {
    return {};
}

AssetImportResult DefaultAssetImporter::import(const AssetImportContext& context,
                                               const AssetImportSettings&) const {
    const auto source = FILE_SYSTEM.readBinary(context.sourcePath);
    if (!source) {
        Log::error("GenericAsset",
                   "Cannot read source file: %s",
                   context.sourcePath.string().c_str());
        return AssetImportResult::failed(AssetType::Generic, "Cannot read source file");
    }

    GenericAsset asset;
    asset.data = *source;
    return writeAssetArtifact(context, asset, AssetType::Generic);
}

} // namespace engine
