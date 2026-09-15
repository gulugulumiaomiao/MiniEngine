#pragma once

#include "asset/importer/AssetImporter.h"

namespace engine {

// Mesh 导入当前没有可调项；保留类型作为后续扩展点（如 scaleFactor / recalculateNormals）。
class MeshImportSettings final : public AssetImportSettingsBase<MeshImportSettings> {
public:
    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Hash64 hash() const override;
};

class MeshAssetImporter final : public AssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Mesh; }
    [[nodiscard]] std::uint32_t version() const override { return 2; }
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const override;
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const override;
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const override;
};

} // namespace engine
