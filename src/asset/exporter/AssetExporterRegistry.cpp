#include "asset/exporter/AssetExporterRegistry.h"

#include "core/logging/Log.h"

namespace engine {

bool AssetExporterRegistry::registerExporter(std::unique_ptr<AssetExporter> exporter) {
    if (!exporter || exporter->assetType() == AssetType::Unknown) {
        Log::error("AssetExporterRegistry", "Cannot register invalid Exporter");
        return false;
    }
    const AssetType type = exporter->assetType();
    const auto [entry, inserted] = exporters_.emplace(type, std::move(exporter));
    (void)entry;
    if (!inserted) {
        Log::error("AssetExporterRegistry", "Exporter already registered: %s", assetTypeName(type));
    }
    return inserted;
}

const AssetExporter* AssetExporterRegistry::find(AssetType type) const {
    const auto found = exporters_.find(type);
    return found == exporters_.end() ? nullptr : found->second.get();
}

void AssetExporterRegistry::clear() {
    exporters_.clear();
}

} // namespace engine
