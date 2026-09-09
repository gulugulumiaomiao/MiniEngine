#pragma once

#include "asset/importer/IAssetImporter.h"

namespace engine {

class TextureAssetImporter final : public IAssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override { return AssetType::Texture; }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    [[nodiscard]] AssetImportResult import(const AssetImportContext& context) const override;
};

} // namespace engine
