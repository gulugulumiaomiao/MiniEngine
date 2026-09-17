#pragma once

#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/DefaultAssetImporter.h"
#include "core/base/Singleton.h"

#include <functional>
#include <memory>
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

    // 注册接管特定源扩展名的脚本导入器；同一扩展名仅允许一个，优先级高于内置
    // 路由。需在 initialize 之后调用；随 shutdown 一并清除。
    [[nodiscard]] bool registerScriptedImporter(std::unique_ptr<ScriptedImporter> importer);

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

    // 内置类型或 ScriptedImporter 接管的扩展名；scanAll/文件监听只处理这些源。
    // DefaultImporter 透传不参与扫描，只有显式 importAsset/依赖声明会路由过去。
    [[nodiscard]] bool isKnownSourceAsset(const VirtualPath& path) const;
    [[nodiscard]] bool importAssetInternal(const VirtualPath& sourcePath, bool force);
    [[nodiscard]] bool renameAsset(const VirtualPath& oldPath, const VirtualPath& newPath);
    void notify(const AssetImportNotification& notification) const;

    AssetImporterRegistry registry_;
    DefaultAssetImporter defaultImporter_;
    std::unordered_set<std::string> importing_;
    Listener listener_;
    mutable std::recursive_mutex mutex_;
    bool initialized_{};
};

} // namespace engine

#define ASSET_IMPORT_PIPELINE (::engine::AssetImportPipeline::instance())
