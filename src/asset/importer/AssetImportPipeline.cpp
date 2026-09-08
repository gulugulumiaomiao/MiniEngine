#include "asset/importer/AssetImportPipeline.h"

#include "core/filesystem/FileDependencyGraph.h"

#include "asset/derived_data/AssetArtifact.h"
#include "asset/base/AssetMeta.h"
#include "asset/importer/FileWatcher.h"
#include "asset/importer/BuiltinAssetImporters.h"
#include "core/logging/Log.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ranges>

namespace engine {
namespace {

[[nodiscard]] std::uint64_t hashContent(std::span<const std::byte> bytes) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const std::byte byte : bytes) {
        value ^= static_cast<std::uint8_t>(byte);
        value *= 1099511628211ULL;
    }
    return value;
}

[[nodiscard]] std::optional<std::uint64_t> hashFile(const VirtualPath& path) {
    const auto bytes = FILE_SYSTEM.readBinary(path);
    return bytes ? std::optional<std::uint64_t>{hashContent(*bytes)} : std::nullopt;
}

[[nodiscard]] bool isSourceAsset(const VirtualPath& path) {
    return inferAssetType(path) != AssetType::Unknown;
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

bool AssetImportPipeline::initialized() const {
    std::scoped_lock lock{mutex_};
    return initialized_;
}

void AssetImportPipeline::setListener(Listener listener) {
    std::scoped_lock lock{mutex_};
    listener_ = std::move(listener);
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
    for (const VirtualPath& path : FILE_SYSTEM.listFiles(VirtualPath{"asset://"}, true)) {
        if (isSourceAsset(path))
            sources.push_back(path);
    }
    std::ranges::sort(sources, {}, [](const VirtualPath& path) {
        return std::pair{inferAssetType(path) == AssetType::Material, path.string()};
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

bool AssetImportPipeline::ensureMaterialShaderImported(const VirtualPath& materialPath) {
    const auto source = FILE_SYSTEM.readText(materialPath);
    if (!source)
        return false;
    const std::shared_ptr<MaterialAsset> material =
        detail::parseMaterialAsset(materialPath, *source);
    if (!material)
        return false;
    if (inferAssetType(material->shader) != AssetType::Shader) {
        Log::error("AssetImportPipeline",
                   "Material Shader path is invalid: %s",
                   material->shader.string().c_str());
        return false;
    }
    return importAssetInternal(material->shader, false);
}

bool AssetImportPipeline::importAssetInternal(const VirtualPath& sourcePath, bool force) {
    if (!initialized_ || !sourcePath.valid() || !isSourceAsset(sourcePath)) {
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

    const AssetType inferredType = inferAssetType(sourcePath);
    const VirtualPath metaPath = assetMetaPath(sourcePath);
    std::optional<AssetMeta> meta =
        FILE_SYSTEM.isFile(metaPath) ? loadAssetMeta(metaPath) : createAssetMeta(sourcePath);
    if (!meta || meta->assetType != inferredType) {
        Log::error("AssetImportPipeline", "Invalid Meta for asset: %s", key.c_str());
        return false;
    }
    const IAssetImporter* importer = registry_.find(meta->assetType);
    if (!importer) {
        Log::error("AssetImportPipeline", "No Importer for asset: %s", key.c_str());
        return false;
    }
    const VirtualPath artifactPath = ASSET_DATABASE.artifactPath(meta->assetId);
    const auto sourceHash = hashFile(sourcePath);
    const auto metaHash = hashFile(metaPath);
    if (!sourceHash || !metaHash)
        return false;

    const auto existing = ASSET_DATABASE.findByPath(sourcePath);
    if (!force && existing && existing->id == meta->assetId &&
        existing->status == AssetImportStatus::Imported &&
        existing->importerVersion == importer->version() && existing->sourceHash == *sourceHash &&
        existing->metaHash == *metaHash && FILE_SYSTEM.isFile(existing->artifactPath)) {
        return true;
    }

    AssetImportContext context{*meta, sourcePath, metaPath, artifactPath};
    AssetImportResult result =
        meta->assetType == AssetType::Material && !ensureMaterialShaderImported(sourcePath)
            ? AssetImportResult::failed(AssetType::Material,
                                        "Material Shader dependency import failed")
            : importer->import(context);
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
    if (result.success)
        record.dependencies = std::move(result.dependencies);
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
        if (isSourceAsset(dependency)) {
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
            if (isSourceAsset(dependent))
                (void)importAssetInternal(dependent, true);
            reimportDependents(dependent);
        }
    };
    const auto consume = [this, &sourceForMeta, &reimportDependents](const VirtualPath& path,
                                                                     FileChangeType type) {
        FILE_DEPENDENCY_GRAPH.notifyChanged(path);
        if (isSourceAsset(path)) {
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
            consume(event.previousPath, FileChangeType::Removed);
            consume(event.path, FileChangeType::Added);
        } else {
            consume(event.path, event.type);
        }
    }
}

} // namespace engine
