#pragma once

#include "asset/exporter/AssetExporter.h"

namespace engine {

// Generic 透传写回（DefaultAssetImporter 的对称物）：GenericAsset.data 的字节
// 原样写回源文件，不做任何转换。
class GenericAssetExporter final : public AssetExporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Generic; }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    // .meta 是导入管线的伴生文件，永远不是资产本体。
    [[nodiscard]] bool supports(const VirtualPath& targetPath) const override;
    [[nodiscard]] AssetExportResult
    write(const Asset& asset, const VirtualPath& targetPath) const override;
};

} // namespace engine
