#pragma once

#include "asset/importer/AssetImporter.h"

namespace engine {

// Shader 导入当前没有可调项；保留类型作为后续扩展点（如导入期 include 扫描开关）。
class ShaderImportSettings final : public AssetImportSettingsBase<ShaderImportSettings> {
public:
    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Hash64 hash() const override;
};

class ShaderAssetImporter final : public AssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Shader; }
    [[nodiscard]] std::uint32_t version() const override { return 3; }
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const override;
    // GLSL include 由 ShaderCompilePipeline 在运行时追踪，导入期依赖保持为空。
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const override;
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const override;
};

} // namespace engine
