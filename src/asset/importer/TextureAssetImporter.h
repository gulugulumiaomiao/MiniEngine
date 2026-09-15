#pragma once

#include "asset/importer/AssetImporter.h"

namespace engine {

// Texture 导入设置：generateMipmaps 控制普通图片（PNG/JPG）解码后是否生成完整
// Mip 链；KTX 容器自带 Mip 数据，不受此项影响。
class TextureImportSettings final : public AssetImportSettingsBase<TextureImportSettings> {
public:
    bool generateMipmaps{true};

    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] Hash64 hash() const override;
};

class TextureAssetImporter final : public AssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Texture; }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    [[nodiscard]] std::unique_ptr<AssetImportSettings>
    createDefaultSettings(const VirtualPath& sourcePath) const override;
    [[nodiscard]] std::vector<VirtualPath>
    gatherDependencies(const AssetImportContext& context,
                       const AssetImportSettings& settings) const override;
    [[nodiscard]] AssetImportResult
    import(const AssetImportContext& context, const AssetImportSettings& settings) const override;
};

} // namespace engine
