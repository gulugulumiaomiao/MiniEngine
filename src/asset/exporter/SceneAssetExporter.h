#pragma once

#include "asset/exporter/AssetExporter.h"

namespace engine {

// .scene.json 写回：format::writeSceneAssetJson 编码后原子写入。
// 运行时 Scene -> SceneAsset 的提取沿用 scene 模块的 exportSceneToAsset。
class SceneAssetExporter final : public AssetExporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Scene; }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    [[nodiscard]] bool supports(const VirtualPath& targetPath) const override;
    [[nodiscard]] AssetExportResult
    write(const Asset& asset, const VirtualPath& targetPath) const override;
};

} // namespace engine
