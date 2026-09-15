#include "asset/importer/AssetImporterRegistry.h"

#include "core/logging/Log.h"

namespace engine {

bool AssetImporterRegistry::registerImporter(std::unique_ptr<AssetImporter> importer) {
    if (!importer || importer->assetType() == AssetType::Unknown) {
        Log::error("AssetImporterRegistry", "Cannot register invalid Importer");
        return false;
    }
    const AssetType type = importer->assetType();
    const auto [entry, inserted] = importers_.emplace(type, std::move(importer));
    (void)entry;
    if (!inserted) {
        Log::error("AssetImporterRegistry", "Importer already registered: %s", assetTypeName(type));
    }
    return inserted;
}

bool AssetImporterRegistry::registerScriptedImporter(std::unique_ptr<ScriptedImporter> importer) {
    if (!importer || importer->assetType() == AssetType::Unknown) {
        Log::error("AssetImporterRegistry", "Cannot register invalid ScriptedImporter");
        return false;
    }
    const std::string extension = lowercaseExtension(importer->sourceExtension());
    if (extension.size() < 2 || extension.front() != '.') {
        Log::error("AssetImporterRegistry",
                   "ScriptedImporter extension must start with '.': %s",
                   extension.c_str());
        return false;
    }
    const auto [entry, inserted] = scriptedImporters_.emplace(extension, std::move(importer));
    (void)entry;
    if (!inserted) {
        Log::error("AssetImporterRegistry",
                   "ScriptedImporter already registered: %s",
                   extension.c_str());
    }
    return inserted;
}

const AssetImporter* AssetImporterRegistry::find(AssetType type) const {
    const auto found = importers_.find(type);
    return found == importers_.end() ? nullptr : found->second.get();
}

const ScriptedImporter* AssetImporterRegistry::findScripted(const VirtualPath& sourcePath) const {
    if (!sourcePath.valid())
        return nullptr;
    // 后缀匹配支持多级扩展名（如 .shader.json），更长（更具体）的注册优先。
    // supports() 默认按同一规则匹配；派生类可按文件头等做进一步过滤。
    const ScriptedImporter* best = nullptr;
    std::size_t bestLength = 0;
    for (const auto& [extension, importer] : scriptedImporters_) {
        if (extension.size() > bestLength && sourceExtensionMatches(sourcePath, extension) &&
            importer->supports(sourcePath)) {
            best = importer.get();
            bestLength = extension.size();
        }
    }
    return best;
}

void AssetImporterRegistry::clear() {
    importers_.clear();
    scriptedImporters_.clear();
}

} // namespace engine
