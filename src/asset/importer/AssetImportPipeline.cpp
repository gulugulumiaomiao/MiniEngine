#include "asset/importer/AssetImportPipeline.h"

#include "core/filesystem/FileDependencyGraph.h"
#include "core/math/hash.h"

#include "asset/derived_data/AssetArtifact.h"
#include "asset/base/AssetMeta.h"
#include "core/filesystem/FileWatcher.h"
#include "asset/importer/BuiltinAssetImporters.h"
#include "core/logging/Log.h"
#include "core/filesystem/FileSystem.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ranges>

namespace engine {
namespace {

// 依赖源文件的当前哈希与记录快照一致时，资产不因依赖变化而重导入。
// 快照缺失或长度不符（旧记录）同样视为不一致，触发一次重导入后即自愈。
[[nodiscard]] bool dependencySnapshotsMatch(const AssetRecord& record) {
    if (record.dependencies.size() != record.dependencyHashes.size())
        return false;
    for (std::size_t index = 0; index < record.dependencies.size(); ++index) {
        const auto hash = hashFile(record.dependencies[index]);
        if (!hash || *hash != record.dependencyHashes[index])
            return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::uint64_t>
currentDependencyHashes(const std::vector<VirtualPath>& dependencies) {
    std::vector<std::uint64_t> hashes;
    hashes.reserve(dependencies.size());
    for (const VirtualPath& dependency : dependencies) {
        hashes.push_back(hashFile(dependency).value_or(0));
    }
    return hashes;
}

} // namespace

bool AssetImportPipeline::initialize() {
    std::scoped_lock lock{mutex_};
    if (initialized_)
        return true;
    registry_.clear();
    if (!registerBuiltinAssetImporters(registry_) || !ASSET_DATABASE.initialize()) {
        Log::error("AssetImportPipeline", "Initialization failed");
        registry_.clear();
        return false;
    }
    initialized_ = true;
    return true;
}

void AssetImportPipeline::shutdown() {
    std::scoped_lock lock{mutex_};
    if (!initialized_)
        return;
    (void)ASSET_DATABASE.save();
    importing_.clear();
    listener_ = {};
    registry_.clear();
    initialized_ = false;
}

bool AssetImportPipeline::registerScriptedImporter(std::unique_ptr<ScriptedImporter> importer) {
    std::scoped_lock lock{mutex_};
    if (!initialized_) {
        Log::error("AssetImportPipeline", "Pipeline is not initialized");
        return false;
    }
    return registry_.registerScriptedImporter(std::move(importer));
}

bool AssetImportPipeline::initialized() const {
    std::scoped_lock lock{mutex_};
    return initialized_;
}

void AssetImportPipeline::setListener(Listener listener) {
    std::scoped_lock lock{mutex_};
    listener_ = std::move(listener);
}

bool AssetImportPipeline::isKnownSourceAsset(const VirtualPath& path) const {
    return inferAssetType(path) != AssetType::Unknown || registry_.findScripted(path) != nullptr;
}

void AssetImportPipeline::notify(const AssetImportNotification& notification) const {
    if (listener_)
        listener_(notification);
}

bool AssetImportPipeline::scanAll() {
    std::scoped_lock lock{mutex_};
    if (!initialized_) {
        Log::error("AssetImportPipeline", "Pipeline is not initialized");
        return false;
    }
    std::vector<VirtualPath> sources;
    for (const VirtualPath& path : FILE_SYSTEM.listFiles(VirtualPath{"assets://"}, true)) {
        if (isKnownSourceAsset(path))
            sources.push_back(path);
    }
    std::ranges::sort(sources, {}, [](const VirtualPath& path) {
        const AssetType type = inferAssetType(path);
        // Import order: shaders/textures/meshes first, then materials (which
        // reference them), then scenes (which reference materials/meshes).
        // ScriptedImporter extensions infer as Unknown and land in group 0,
        // ahead of the assets that reference them.
        if (type == AssetType::Scene)
            return 2;
        if (type == AssetType::Material)
            return 1;
        return 0;
    });
    bool success = true;
    std::unordered_set<std::string> present;
    for (const VirtualPath& source : sources) {
        present.insert(source.string());
        success = importAssetInternal(source, false) && success;
    }
    for (const AssetRecord& record : ASSET_DATABASE.records()) {
        if (!present.contains(record.sourcePath.string())) {
            success = removeAsset(record.sourcePath) && success;
        }
    }
    return ASSET_DATABASE.save() && success;
}

bool AssetImportPipeline::importAsset(const VirtualPath& sourcePath) {
    std::scoped_lock lock{mutex_};
    return importAssetInternal(sourcePath, false);
}

bool AssetImportPipeline::reimportAsset(const VirtualPath& sourcePath) {
    std::scoped_lock lock{mutex_};
    return importAssetInternal(sourcePath, true);
}

bool AssetImportPipeline::importAssetInternal(const VirtualPath& sourcePath, bool force) {
    if (!initialized_ || !sourcePath.valid() || !isAssetScheme(sourcePath.scheme())) {
        Log::error(
            "AssetImportPipeline", "Unsupported asset path: %s", sourcePath.string().c_str());
        return false;
    }
    const std::string key = sourcePath.string();
    if (!importing_.insert(key).second) {
        Log::error("AssetImportPipeline", "Cyclic asset dependency: %s", key.c_str());
        return false;
    }
    struct ImportGuard {
        std::unordered_set<std::string>& set;
        std::string key;
        ~ImportGuard() { set.erase(key); }
    } guard{importing_, key};

    // 路由优先级：ScriptedImporter 扩展名接管 > 内置类型推断 > DefaultImporter
    // 透传（Generic）。路由结果决定 Meta 里的 assetType，因此必须先定 importer
    // 再建 Meta，不能反过来用 find(meta->assetType) 二次查找。
    const ScriptedImporter* scripted = registry_.findScripted(sourcePath);
    const AssetImporter* importer = scripted;
    AssetType assetType = scripted ? scripted->assetType() : inferAssetType(sourcePath);
    if (!importer && assetType != AssetType::Unknown)
        importer = registry_.find(assetType);
    if (!importer) {
        importer = &defaultImporter_;
        assetType = importer->assetType();
    }
    if (!importer->supports(sourcePath)) {
        Log::error("AssetImportPipeline", "No Importer for asset: %s", key.c_str());
        return false;
    }

    const VirtualPath metaPath = assetMetaPath(sourcePath);
    std::optional<AssetMeta> meta;
    if (FILE_SYSTEM.isFile(metaPath)) {
        meta = loadAssetMeta(metaPath);
        if (!meta) {
            // Unparseable Meta: regenerate with a new random GUID.
            Log::warn("AssetImportPipeline",
                      "Discarding invalid Meta for %s; regenerating",
                      key.c_str());
            meta = createAssetMeta(sourcePath, assetType);
        } else if (meta->assetType != assetType) {
            // Routed type changed (e.g. ScriptedImporter took over this extension).
            // Keep the existing GUID so the asset identity stays stable, but update
            // the stored asset type. Any stale database record under this path is
            // removed first so the GUID change does not collide.
            Log::warn("AssetImportPipeline",
                      "Updating Meta asset type for %s (%s -> %s)",
                      key.c_str(),
                      assetTypeName(meta->assetType),
                      assetTypeName(assetType));
            (void)ASSET_DATABASE.remove(sourcePath);
            meta->assetType = assetType;
            if (!saveAssetMeta(metaPath, *meta)) {
                Log::error("AssetImportPipeline", "Cannot update Meta for %s", key.c_str());
                return false;
            }
        }
    } else {
        meta = createAssetMeta(sourcePath, assetType);
    }
    if (!meta) {
        Log::error("AssetImportPipeline", "Invalid Meta for asset: %s", key.c_str());
        return false;
    }
    const VirtualPath artifactPath = ASSET_DATABASE.artifactPath(meta->assetId);
    const auto sourceHash = hashFile(sourcePath);
    const auto metaHash = hashFile(metaPath);
    if (!sourceHash || !metaHash)
        return false;

    const std::unique_ptr<AssetImportSettings> settings =
        importer->createDefaultSettings(sourcePath);
    if (!settings) {
        Log::error(
            "AssetImportPipeline", "Importer produced no default settings: %s", key.c_str());
        return false;
    }

    const auto existing = ASSET_DATABASE.findByPath(sourcePath);
    if (!force && existing && existing->id == meta->assetId &&
        existing->status == AssetImportStatus::Imported &&
        existing->importerVersion == importer->version() && existing->sourceHash == *sourceHash &&
        existing->metaHash == *metaHash && existing->settingsHash == settings->hash() &&
        dependencySnapshotsMatch(*existing) && FILE_SYSTEM.isFile(existing->artifactPath)) {
        return true;
    }

    AssetImportContext context{*meta, sourcePath, metaPath, artifactPath};

    // 通用依赖驱动调度：先把 importer 通过 gatherDependencies 声明的依赖逐个导入。
    // 递归的 importAssetInternal 构成依赖 DAG 的后序遍历（叶子最先落盘）；入口处的
    // importing_ 集合同时拦截依赖声明中出现的循环依赖。
    bool dependenciesImported = true;
    std::string dependencyError;
    for (const VirtualPath& dependency : importer->gatherDependencies(context, *settings)) {
        if (!importAssetInternal(dependency, false)) {
            dependenciesImported = false;
            dependencyError = "Dependency import failed: " + dependency.string();
            break;
        }
    }
    AssetImportResult result =
        dependenciesImported
            ? importer->import(context, *settings)
            : AssetImportResult::failed(meta->assetType, std::move(dependencyError));
    AssetRecord record = existing.value_or(AssetRecord{});
    record.id = meta->assetId;
    record.type = meta->assetType;
    record.sourcePath = sourcePath;
    record.metaPath = metaPath;
    record.artifactPath = artifactPath;
    record.importerVersion = importer->version();
    record.sourceHash = *sourceHash;
    record.metaHash = *metaHash;
    if (result.success) {
        const auto artifactHash = hashFile(artifactPath);
        if (!artifactHash)
            result =
                AssetImportResult::failed(meta->assetType, "Importer did not produce an Artifact");
        else
            record.artifactHash = *artifactHash;
    }
    record.status = result.success ? AssetImportStatus::Imported : AssetImportStatus::Failed;
    record.lastError = result.error;
    if (result.success) {
        record.settingsHash = settings->hash();
        record.dependencies = std::move(result.dependencies);
        record.dependencyHashes = currentDependencyHashes(record.dependencies);
    }
    if (!ASSET_DATABASE.addOrUpdate(record) || !ASSET_DATABASE.save()) {
        Log::error("AssetImportPipeline", "Cannot update AssetDatabase: %s", key.c_str());
        return false;
    }
    notify({sourcePath, meta->assetType, result.success, false});
    return result.success;
}

bool AssetImportPipeline::importDependencies(const VirtualPath& sourcePath) {
    std::scoped_lock lock{mutex_};
    bool success = true;
    for (const VirtualPath& dependency : ASSET_DATABASE.dependenciesOf(sourcePath)) {
        if (isKnownSourceAsset(dependency)) {
            success = importAssetInternal(dependency, false) && success;
        }
    }
    return success;
}

bool AssetImportPipeline::removeAsset(const VirtualPath& sourcePath) {
    std::scoped_lock lock{mutex_};
    const auto record = ASSET_DATABASE.findByPath(sourcePath);
    if (!record)
        return true;
    if (FILE_SYSTEM.isFile(record->artifactPath)) {
        (void)FILE_SYSTEM.removeFile(record->artifactPath);
    }
    if (!ASSET_DATABASE.remove(sourcePath) || !ASSET_DATABASE.save()) {
        return false;
    }
    notify({sourcePath, record->type, true, true});
    return true;
}

bool AssetImportPipeline::renameAsset(const VirtualPath& oldPath, const VirtualPath& newPath) {
    // Caller must already hold mutex_ (processFileEvents).
    const VirtualPath oldMetaPath = assetMetaPath(oldPath);
    const VirtualPath newMetaPath = assetMetaPath(newPath);

    auto record = ASSET_DATABASE.findByPath(oldPath);
    if (!record) {
        // Unknown source: treat the new path as a fresh add.
        return importAssetInternal(newPath, false);
    }

    // Move the .meta sidecar so the GUID is preserved. If the old .meta is missing
    // and the new path already has one, trust it; otherwise regenerate (new GUID).
    if (FILE_SYSTEM.isFile(oldMetaPath)) {
        if (!FILE_SYSTEM.move(oldMetaPath, newMetaPath)) {
            Log::error("AssetImportPipeline",
                       "Cannot move Meta from %s to %s",
                       oldMetaPath.string().c_str(),
                       newMetaPath.string().c_str());
            return false;
        }
    } else if (!FILE_SYSTEM.isFile(newMetaPath)) {
        if (!createAssetMeta(newPath, record->type)) {
            return false;
        }
    }

    // Update path mappings while keeping the GUID and artifact directory intact.
    record->sourcePath = newPath;
    record->metaPath = newMetaPath;
    if (!ASSET_DATABASE.addOrUpdate(*record) || !ASSET_DATABASE.save()) {
        Log::error("AssetImportPipeline",
                   "Cannot update AssetDatabase for renamed asset: %s -> %s",
                   oldPath.string().c_str(),
                   newPath.string().c_str());
        return false;
    }

    // Re-import at the new path to refresh content hashes and notify dependents.
    if (!importAssetInternal(newPath, true)) {
        return false;
    }
    return true;
}

void AssetImportPipeline::processFileEvents() {
    std::scoped_lock lock{mutex_};
    if (!initialized_)
        return;

    const auto sourceForMeta = [](const VirtualPath& path) {
        constexpr std::string_view suffix{".meta"};
        return path.string().ends_with(suffix)
                   ? VirtualPath{path.string().substr(0, path.string().size() - suffix.size())}
                   : VirtualPath{};
    };
    std::unordered_set<std::string> cascaded;
    std::function<void(const VirtualPath&)> reimportDependents;
    reimportDependents = [this, &cascaded, &reimportDependents](const VirtualPath& dependency) {
        if (!cascaded.insert(dependency.string()).second)
            return;
        for (const VirtualPath& dependent : ASSET_DATABASE.dependentsOf(dependency)) {
            if (isKnownSourceAsset(dependent))
                (void)importAssetInternal(dependent, true);
            reimportDependents(dependent);
        }
    };
    const auto consume = [this, &sourceForMeta, &reimportDependents](const VirtualPath& path,
                                                                     FileChangeType type) {
        FILE_DEPENDENCY_GRAPH.notifyChanged(path);
        if (isKnownSourceAsset(path)) {
            if (type == FileChangeType::Removed) {
                reimportDependents(path);
                (void)removeAsset(path);
            } else if (importAssetInternal(path, type == FileChangeType::Modified)) {
                reimportDependents(path);
            }
            return;
        }
        const VirtualPath metaSource = sourceForMeta(path);
        if (metaSource.valid() && FILE_SYSTEM.isFile(metaSource)) {
            (void)importAssetInternal(metaSource, true);
            return;
        }
        reimportDependents(path);
    };

    for (const FileChangeEvent& event : FILE_WATCHER.pollEvents()) {
        if (event.type == FileChangeType::Renamed) {
            FILE_DEPENDENCY_GRAPH.notifyChanged(event.previousPath);
            FILE_DEPENDENCY_GRAPH.notifyChanged(event.path);
            reimportDependents(event.previousPath);
            if (renameAsset(event.previousPath, event.path)) {
                reimportDependents(event.path);
            }
        } else {
            consume(event.path, event.type);
        }
    }
}

} // namespace engine
