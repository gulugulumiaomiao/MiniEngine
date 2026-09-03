#include "asset/importer/AssetImporterRegistry.h"

#include "core/Log.h"

namespace engine {

bool AssetImporterRegistry::registerImporter(
    std::unique_ptr<IAssetImporter> importer) {
    if (!importer || importer->assetType() == AssetType::Unknown) {
        Log::error("AssetImporterRegistry", "Cannot register invalid Importer");
        return false;
    }
    const AssetType type = importer->assetType();
    const auto [entry, inserted] = importers_.emplace(type, std::move(importer));
    (void)entry;
    if (!inserted) {
        Log::error("AssetImporterRegistry", "Importer already registered: %s",
                   assetTypeName(type));
    }
    return inserted;
}

const IAssetImporter* AssetImporterRegistry::find(AssetType type) const {
    const auto found = importers_.find(type);
    return found == importers_.end() ? nullptr : found->second.get();
}

void AssetImporterRegistry::clear() {
    importers_.clear();
}

} // namespace engine
