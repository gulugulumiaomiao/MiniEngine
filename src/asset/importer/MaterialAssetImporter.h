#pragma once

#include "asset/importer/AssetImporter.h"

namespace engine {

// Material 导入当前没有可调项；保留类型作为后续扩展点。
class MaterialImportSettings final : public AssetImportSettingsBase<MaterialImportSettings> {
public:
    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Hash64 hash() const override;
};

class MaterialAssetImporter final : public AssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Material; }
    [[nodiscard]] std::uint32_t version() const override { return 4; }
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const override;
    // 声明 Shader 与实际引用的 Texture；管线保证它们先导入。
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const override;
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const override;
};

} // namespace engine
