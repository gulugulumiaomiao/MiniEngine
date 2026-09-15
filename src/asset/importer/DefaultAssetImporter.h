#pragma once

#include "asset/importer/AssetImporter.h"

namespace engine {

// DefaultImporter 的透传没有可调项；保留类型作为后续扩展点（如按大小过滤）。
class GenericImportSettings final : public AssetImportSettingsBase<GenericImportSettings> {
public:
    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Hash64 hash() const override;
};

// 兜底导入器（对齐 Unity DefaultImporter）：没有专用 Importer 的源文件按
// Generic 资产透传——源字节原样进 Artifact，GUID/依赖哈希照常记录，因此
// 编辑器与打包管线可以稳定引用任意资产。它是 Pipeline 内置的 fallback，
// 不进注册表；scanAll 不做兜底扫描，只有显式 importAsset/依赖声明会路由到这里。
class DefaultAssetImporter final : public AssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Generic; }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    // .meta 是导入管线的伴生文件，永远不是资产本体。
    [[nodiscard]] bool supports(const VirtualPath& sourcePath) const override;
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const override;
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const override;
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const override;
};

} // namespace engine
