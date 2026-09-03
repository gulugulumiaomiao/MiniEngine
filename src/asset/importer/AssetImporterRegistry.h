#pragma once

#include "asset/importer/IAssetImporter.h"

#include <memory>
#include <unordered_map>

namespace engine {

class AssetImporterRegistry final {
public:
    [[nodiscard]] bool registerImporter(
        std::unique_ptr<IAssetImporter> importer);
    [[nodiscard]] const IAssetImporter* find(AssetType type) const;
    void clear();

private:
    std::unordered_map<AssetType, std::unique_ptr<IAssetImporter>> importers_;
};

} // namespace engine
