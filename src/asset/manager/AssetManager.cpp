#include "asset/manager/AssetManager.h"
#include "asset/types/MaterialAsset.h"
#include "asset/types/ShaderAsset.h"

#include "asset/base/AssetMeta.h"
#include "asset/base/GenericAsset.h"
#include "asset/derived_data/AssetArtifact.h"
#include "asset/database/AssetDatabase.h"
#include "asset/exporter/AssetExportPipeline.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/filesystem/FileWatcher.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "render/texture/Texture.h"
#include "scene/scene/SceneAsset.h"

namespace engine {

bool AssetManager::initialize() {
#if defined(MINI_PUBLISH)
    return initialize(AssetManagerMode::Packaged);
#else
    return initialize(AssetManagerMode::Development);
#endif
}

bool AssetManager::initialize(AssetManagerMode mode) {
    shutdown();
    mode_ = mode;
    if (!FILE_SYSTEM.isDirectory(VirtualPath{"assets://"})) {
        Log::error("AssetManager", "assets:// must be mounted before initialization");
        return false;
    }

    if (mode_ == AssetManagerMode::Packaged) {
        if (!ASSET_DATABASE.initialize()) {
            Log::error("AssetManager", "Cannot load cooked AssetDatabase");
            return false;
        }
        return true;
    }

    if (!ASSET_IMPORT_PIPELINE.initialize()) {
        Log::error("AssetManager", "Cannot initialize asset import pipeline");
        return false;
    }
    if (!ASSET_IMPORT_PIPELINE.scanAll()) {
        Log::error("AssetManager", "Initial asset import completed with errors");
    }
    // 写回管线与导入管线对称挂载：同样的 Development-only 生命周期（Packaged 只读，
    // 不提供写回）。
    if (!ASSET_EXPORT_PIPELINE.initialize()) {
        Log::error("AssetManager", "Cannot initialize asset export pipeline");
        shutdown();
        return false;
    }
    ASSET_IMPORT_PIPELINE.setListener([this](const AssetImportNotification& notification) {
        if (!notification.success)
            return;
        if (notification.type == AssetType::Texture && !notification.removed) {
            // 纹理就地重传：同一缓存 asset 重新 transfer → syncInstance 推送唯一实例，保持活链接
            // （若 invalidate+新建 asset，则持有旧 asset 的实例会失联）。
            reloadInPlace(notification.path);
        } else {
            invalidate(notification.path);
            if (notification.type == AssetType::Shader && !notification.removed) {
                const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(notification.path);
                if (assetId && SHADER_RESOURCE_MANAGER.find(*assetId) &&
                    SHADER_RESOURCE_MANAGER.replace(notification.path)) {
                    MATERIAL_RESOURCE_MANAGER.refreshShader(*assetId);
                }
            } else if (notification.type == AssetType::Mesh && !notification.removed &&
                       MESH_RESOURCE_MANAGER.find(notification.path)) {
                (void)MESH_RESOURCE_MANAGER.replace(notification.path);
            }
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
    return true;
}

void AssetManager::shutdown() {
    changeListener_ = {};
    clear();
    ASSET_IMPORT_PIPELINE.shutdown();
    ASSET_EXPORT_PIPELINE.shutdown();
    FILE_WATCHER.stop();
    ASSET_DATABASE.shutdown();
}

bool AssetManager::ensureImported(const VirtualPath& path) {
    auto record = ASSET_DATABASE.findByPath(path);
    if (record && record->type != AssetType::Unknown &&
        record->status == AssetImportStatus::Imported && FILE_SYSTEM.isFile(record->artifactPath)) {
        return true;
    }
    if (mode_ == AssetManagerMode::Packaged) {
        Log::error("AssetManager", "Cooked Artifact is unavailable: %s", path.string().c_str());
        return false;
    }
    if (!ASSET_IMPORT_PIPELINE.initialized() || !ASSET_IMPORT_PIPELINE.importAsset(path)) {
        Log::error("AssetManager", "Asset import failed: %s", path.string().c_str());
        return false;
    }
    record = ASSET_DATABASE.findByPath(path);
    return record && record->type != AssetType::Unknown &&
           record->status == AssetImportStatus::Imported &&
           FILE_SYSTEM.isFile(record->artifactPath);
}

Ref<Asset> AssetManager::loadAsset(const VirtualPath& path) {
    if (!path.valid() || !isAssetScheme(path.scheme())) {
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

    Ref<Asset> asset;
    switch (artifact->assetType) {
    case AssetType::Shader: asset = makeRef<ShaderAsset>(); break;
    case AssetType::Material: asset = makeRef<MaterialAsset>(); break;
    case AssetType::Mesh: asset = makeRef<MeshAsset>(); break;
    case AssetType::Texture: asset = makeRef<TextureAsset>(); break;
    case AssetType::Scene: asset = makeRef<SceneAsset>(); break;
    case AssetType::Generic: asset = makeRef<GenericAsset>(); break;
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

void AssetManager::reloadInPlace(const VirtualPath& path) {
    Ref<Asset> cached = findCached(path);
    if (!cached) {
        invalidate(path); // 未加载：清掉可能的陈旧缓存，下次 loadAsset 取新数据
        return;
    }
    if (!ensureImported(path))
        return;
    const auto record = ASSET_DATABASE.findByPath(path);
    const auto artifact = record ? loadAssetArtifact(record->artifactPath) : std::nullopt;
    if (!record || !artifact || artifact->assetId != record->id ||
        artifact->assetType != record->type) {
        Log::error("AssetManager", "Invalid Asset Artifact for reload: %s", path.string().c_str());
        return;
    }
    BinaryReader reader{artifact->payload};
    if (!cached->transfer(reader) || !reader.finished()) {
        Log::error(
            "AssetManager", "Cannot re-deserialize Asset in place: %s", path.string().c_str());
        return;
    }
    // TextureAsset::transfer 的读取分支已调用 syncInstance()，唯一实例已重上传新像素。
}

void AssetManager::clear() {
    std::scoped_lock lock{mutex_};
    cache_.clear();
}

} // namespace engine
