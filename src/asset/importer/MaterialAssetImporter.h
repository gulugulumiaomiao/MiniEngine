#pragma once

#include "asset/importer/IAssetImporter.h"

namespace engine {

class MaterialAssetImporter final : public IAssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override {
        return AssetType::Material;
    }
    [[nodiscard]] std::uint32_t version() const override { return 1; }
    [[nodiscard]] AssetImportResult import(
        const AssetImportContext& context) const override;
};

} // namespace engine
