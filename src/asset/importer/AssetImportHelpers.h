#pragma once

#include "asset/base/Asset.h"
#include "asset/importer/AssetImporter.h"

#include <vector>

namespace engine {

// Serializes an Asset into an Artifact, atomically writes it to the path described by
// `context.artifactPath`, and returns an AssetImportResult carrying the (sorted and
// deduplicated) dependency list. This removes the duplicated BinaryWriter / AssetArtifact
// / saveAssetArtifact boilerplate from every importer.
[[nodiscard]] AssetImportResult writeAssetArtifact(
    const AssetImportContext& context,
    Asset& asset,
    AssetType type,
    std::vector<VirtualPath> dependencies = {});

} // namespace engine
