#pragma once

#include "asset/base/Asset.h"

#include <optional>
#include <span>
#include <vector>

namespace engine {

struct AssetArtifact {
    std::uint32_t version{1};
    AssetId assetId;
    AssetType assetType{AssetType::Unknown};
    VirtualPath sourcePath;
    std::vector<std::byte> payload;
};

[[nodiscard]] std::vector<std::byte> serializeAssetArtifact(const AssetArtifact& artifact);
[[nodiscard]] std::optional<AssetArtifact> parseAssetArtifact(const VirtualPath& artifactPath,
                                                              std::span<const std::byte> source);
[[nodiscard]] bool saveAssetArtifact(const VirtualPath& path, const AssetArtifact& artifact);
[[nodiscard]] std::optional<AssetArtifact> loadAssetArtifact(const VirtualPath& path);

} // namespace engine
