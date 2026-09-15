#pragma once

#include "asset/base/Asset.h"

#include <optional>
#include <string>
#include <string_view>

namespace engine {

struct AssetMeta {
    std::uint32_t version{1};
    AssetId assetId;
    AssetType assetType{AssetType::Unknown};
};

// Virtual path schemes that hold importable source assets. The only asset scope is the
// active project (assets://): the engine's built-in content ships inside every project's
// assets/ (copied at creation and at open), so it imports exactly like any user asset.
[[nodiscard]] bool isAssetScheme(std::string_view scheme);

[[nodiscard]] AssetType inferAssetType(const VirtualPath& sourcePath);
[[nodiscard]] VirtualPath assetMetaPath(const VirtualPath& sourcePath);
[[nodiscard]] std::optional<AssetMeta> parseAssetMeta(const VirtualPath& metaPath,
                                                      std::string_view source);
[[nodiscard]] std::string serializeAssetMeta(const AssetMeta& meta);
[[nodiscard]] std::optional<AssetMeta> loadAssetMeta(const VirtualPath& metaPath);
[[nodiscard]] bool saveAssetMeta(const VirtualPath& metaPath, const AssetMeta& meta);
[[nodiscard]] std::optional<AssetMeta> createAssetMeta(const VirtualPath& sourcePath);
// 显式指定类型的变体：ScriptedImporter 接管的扩展名（如 .obj）无法从路径推断。
[[nodiscard]] std::optional<AssetMeta> createAssetMeta(const VirtualPath& sourcePath, AssetType type);

} // namespace engine
