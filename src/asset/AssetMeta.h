#pragma once

#include "asset/Asset.h"

#include <optional>
#include <string>
#include <string_view>

namespace engine {

struct AssetMeta {
    std::uint32_t version{1};
    AssetId assetId;
    AssetType assetType{AssetType::Unknown};
};

[[nodiscard]] AssetType inferAssetType(const VirtualPath& sourcePath);
[[nodiscard]] VirtualPath assetMetaPath(const VirtualPath& sourcePath);
[[nodiscard]] std::optional<AssetMeta> parseAssetMeta(
    const VirtualPath& metaPath, std::string_view source);
[[nodiscard]] std::string serializeAssetMeta(const AssetMeta& meta);
[[nodiscard]] std::optional<AssetMeta> loadAssetMeta(
    const VirtualPath& metaPath);
[[nodiscard]] bool saveAssetMeta(const VirtualPath& metaPath,
                                 const AssetMeta& meta);
[[nodiscard]] std::optional<AssetMeta> createAssetMeta(
    const VirtualPath& sourcePath);

} // namespace engine
