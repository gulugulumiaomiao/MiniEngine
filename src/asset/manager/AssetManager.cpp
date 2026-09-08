#include "asset/manager/AssetManager.h"

#include "asset/derived_data/AssetArtifact.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "asset/importer/FileWatcher.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "scene/scene/SceneAsset.h"

namespace engine {

bool AssetManager::initialize() {
    shutdown();
    if (!FILE_SYSTEM.isDirectory(VirtualPath{"asset://"})) {
        Log::error("AssetManager", "asset:// must be mounted before initialization");
        return false;
    }

#if defined(MINI_RELEASE)
    if (!ASSET_DATABASE.initialize()) {
        Log::error("AssetManager", "Cannot load cooked AssetDatabase");
        return false;
    }
#else
    if (!ASSET_IMPORT_PIPELINE.initialize()) {
        Log::error("AssetManager", "Cannot initialize asset import pipeline");
        return false;
    }
    if (!ASSET_IMPORT_PIPELINE.scanAll()) {
        Log::error("AssetManager", "Initial asset import completed with errors");
    }
    ASSET_IMPORT_PIPELINE.setListener([this](const AssetImportNotification& notification) {
        if (!notification.success)
            return;
        invalidate(notification.path);
        if (notification.type == AssetType::Shader && !notification.removed) {
            if (SHADER_MANAGER.find(notification.path) &&
                SHADER_MANAGER.replace(notification.path)) {
                MATERIAL_MANAGER.refreshShader(notification.path);
            }
        } else if (notification.type == AssetType::Mesh && !notification.removed &&
                   MESH_MANAGER.find(notification.path)) {
            (void)MESH_MANAGER.replace(notification.path);
        }
        if (changeListener_) {
            changeListener_(notification.path, notification.type, notification.removed);
        }
    });
    if (!FILE_WATCHER.start()) {
        Log::error("AssetManager", "Cannot start asset FileWatcher");
        shutdown();
        return false;
    }
#endif
    return true;
}

void AssetManager::shutdown() {
    changeListener_ = {};
    clear();
    ASSET_IMPORT_PIPELINE.shutdown();
    FILE_WATCHER.stop();
    ASSET_DATABASE.shutdown();
}

bool AssetManager::ensureImported(const VirtualPath& path) {
    auto record = ASSET_DATABASE.findByPath(path);
    if (record && record->type != AssetType::Unknown &&
        record->status == AssetImportStatus::Imported && FILE_SYSTEM.isFile(record->artifactPath)) {
        return true;
    }
#if defined(MINI_RELEASE)
    Log::error("AssetManager", "Cooked Artifact is unavailable: %s", path.string().c_str());
    return false;
#else
    if (!ASSET_IMPORT_PIPELINE.initialized() || !ASSET_IMPORT_PIPELINE.importAsset(path)) {
        Log::error("AssetManager", "Asset import failed: %s", path.string().c_str());
        return false;
    }
    record = ASSET_DATABASE.findByPath(path);
    return record && record->type != AssetType::Unknown &&
           record->status == AssetImportStatus::Imported &&
           FILE_SYSTEM.isFile(record->artifactPath);
#endif
}

std::shared_ptr<Asset> AssetManager::loadAsset(const VirtualPath& path) {
    if (!path.valid() || path.scheme() != "asset") {
        Log::error("AssetManager", "Invalid Asset path: %s", path.string().c_str());
        return {};
    }
    if (auto existing = findCached(path))
        return existing;
    if (!ensureImported(path))
        return {};

    const auto record = ASSET_DATABASE.findByPath(path);
    const auto artifact = record ? loadAssetArtifact(record->artifactPath) : std::nullopt;
    if (!record || !artifact || artifact->assetId != record->id ||
        artifact->assetType != record->type) {
        Log::error("AssetManager", "Invalid Asset Artifact: %s", path.string().c_str());
        return {};
    }

    std::shared_ptr<Asset> asset;
    switch (artifact->assetType) {
    case AssetType::Shader: asset = std::make_shared<ShaderAsset>(); break;
    case AssetType::Material: asset = std::make_shared<MaterialAsset>(); break;
    case AssetType::Mesh: asset = std::make_shared<MeshAsset>(); break;
    case AssetType::Scene: asset = std::make_shared<SceneAsset>(); break;
    default:
        Log::error("AssetManager", "Unsupported Asset type for: %s", path.string().c_str());
        return {};
    }

    asset->setAssetIdentity(record->id, path);
    BinaryReader reader{artifact->payload};
    if (!asset->transfer(reader) || !reader.finished()) {
        Log::error("AssetManager", "Cannot deserialize Asset: %s", path.string().c_str());
        return {};
    }
    return cache(path, std::move(asset));
}

void AssetManager::invalidate(const VirtualPath& path) {
    std::scoped_lock lock{mutex_};
    cache_.erase(path.string());
}

void AssetManager::clear() {
    std::scoped_lock lock{mutex_};
    cache_.clear();
}

} // namespace engine
