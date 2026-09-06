#pragma once

#include "asset/importer/IAssetImporter.h"

namespace engine {

class MeshAssetImporter final : public IAssetImporter {
public:
    [[nodiscard]] AssetType assetType() const override {
        return AssetType::Mesh;
    }
    [[nodiscard]] std::uint32_t version() const override { return 2; }
    [[nodiscard]] AssetImportResult import(
        const AssetImportContext& context) const override;
};

} // namespace engine
