#pragma once

#include "asset/Asset.h"

#include <optional>
#include <string>
#include <string_view>

namespace engine {

struct AssetArtifact {
    std::uint32_t version{1};
    AssetId assetId;
    AssetType assetType{AssetType::Unknown};
    VirtualPath sourcePath;
    std::string payload;
};

[[nodiscard]] std::string serializeAssetArtifact(
    const AssetArtifact& artifact);
[[nodiscard]] std::optional<AssetArtifact> parseAssetArtifact(
    const VirtualPath& artifactPath, std::string_view source);
[[nodiscard]] bool saveAssetArtifact(const VirtualPath& path,
                                     const AssetArtifact& artifact);
[[nodiscard]] std::optional<AssetArtifact> loadAssetArtifact(
    const VirtualPath& path);

} // namespace engine
