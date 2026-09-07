#pragma once

#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImporterRegistry.h"
#include "core/base/Singleton.h"

#include <functional>
#include <mutex>
#include <span>
#include <unordered_set>
#include <vector>

namespace engine {

struct AssetImportNotification {
    VirtualPath path;
    AssetType type{AssetType::Unknown};
    bool success{};
    bool removed{};
};

class AssetImportPipeline final : public Singleton<AssetImportPipeline> {
public:
    using Listener = std::function<void(const AssetImportNotification&)>;

    [[nodiscard]] bool initialize();
    void shutdown();

    [[nodiscard]] bool scanAll();
    [[nodiscard]] bool importAsset(const VirtualPath& sourcePath);
    [[nodiscard]] bool reimportAsset(const VirtualPath& sourcePath);
    [[nodiscard]] bool removeAsset(const VirtualPath& sourcePath);
    [[nodiscard]] bool importDependencies(const VirtualPath& sourcePath);

    void setListener(Listener listener);
    void processFileEvents();
    [[nodiscard]] bool initialized() const;

private:
    friend class Singleton<AssetImportPipeline>;
    AssetImportPipeline() = default;

    [[nodiscard]] bool importAssetInternal(const VirtualPath& sourcePath, bool force);
    [[nodiscard]] bool ensureMaterialShaderImported(const VirtualPath& materialPath);
    void notify(const AssetImportNotification& notification) const;

    AssetImporterRegistry registry_;
    std::unordered_set<std::string> importing_;
    Listener listener_;
    mutable std::recursive_mutex mutex_;
    bool initialized_{};
};

} // namespace engine

#define ASSET_IMPORT_PIPELINE (::engine::AssetImportPipeline::instance())
