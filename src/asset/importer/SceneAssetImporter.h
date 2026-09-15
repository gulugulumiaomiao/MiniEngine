#pragma once

#include "asset/importer/AssetImporter.h"

namespace engine {

// Scene 导入当前没有可调项；保留类型作为后续扩展点。
class SceneImportSettings final : public AssetImportSettingsBase<SceneImportSettings> {
public:
    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Hash64 hash() const override;
};

class SceneAssetImporter final : public AssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Scene; }
    [[nodiscard]] std::uint32_t version() const override { return 2; }
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const override;
    // 声明 Mesh / Material 组件引用的源资产；管线保证它们先导入。
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const override;
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const override;
};

} // namespace engine
