#include "render/material/MaterialManager.h"

#include "asset/types/MaterialAsset.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "render/shader/ShaderManager.h"

#include <vector>

namespace engine {
namespace {

const VirtualPath kErrorMaterialPath{"assets://materials/error.material.json"};

} // namespace

Ref<Material> MaterialResourceManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MaterialResourceManager", "Invalid Material AssetId");
        return errorMaterial();
    }
    if (Ref<Material> existing = find(assetId))
        return existing;
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("MaterialResourceManager",
                   "Unknown Material AssetId: %s",
                   assetId.toString().c_str());
        return errorMaterial();
    }
    return loadFromPath(*path, assetId);
}

Ref<Material> MaterialResourceManager::load(const VirtualPath& materialPath) {
    if (!materialPath.valid()) {
        Log::error("MaterialResourceManager",
                   "Invalid Material path: %s",
                   materialPath.string().c_str());
        return errorMaterial();
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(materialPath);
    if (!assetId) {
        Log::error("MaterialResourceManager",
                   "Material path has no AssetId: %s",
                   materialPath.string().c_str());
        return errorMaterial();
    }
    if (Ref<Material> existing = find(*assetId))
        return existing;
    return loadFromPath(materialPath, *assetId);
}

Ref<Material> MaterialResourceManager::loadFromPath(const VirtualPath& materialPath,
                                                    const AssetId& assetId) {
    Log::info("Material", "Loading material: %s", materialPath.string().c_str());
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    if (!asset)
        return materialPath == kErrorMaterialPath ? Ref<Material>{} : errorMaterial();
    Ref<Shader> shader = SHADER_RESOURCE_MANAGER.load(asset->shader);
    if (!shader)
        return materialPath == kErrorMaterialPath ? Ref<Material>{} : errorMaterial();
    Ref<Material> material = asset->instantiate(shader);
    if (!material)
        return materialPath == kErrorMaterialPath ? Ref<Material>{} : errorMaterial();
    material->assetId_ = assetId;
    return insert(material);
}

Ref<Material> MaterialResourceManager::errorMaterial() {
    if (errorMaterial_)
        return errorMaterial_;
    const std::optional<AssetId> errorId = ASSET_DATABASE.findGuid(kErrorMaterialPath);
    if (!errorId) {
        Log::error("MaterialResourceManager",
                   "Error Material has no AssetId; database may be uninitialized");
        return {};
    }
    errorMaterial_ = load(kErrorMaterialPath);
    return errorMaterial_;
}

Ref<Material> MaterialResourceManager::clone(const Ref<Material>& source) {
    if (!source) {
        Log::error("MaterialResourceManager", "Cannot clone an invalid Material");
        return {};
    }
    return insertUnkeyed(source->clone());
}

void MaterialResourceManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MaterialResourceManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("MaterialResourceManager",
                   "Unknown Material AssetId: %s",
                   assetId.toString().c_str());
        return;
    }
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(*path);
    Ref<Material> material = find(assetId);
    if (!asset || !material) {
        Log::error("MaterialResourceManager",
                   "Failed to reload material asset: %s",
                   path->string().c_str());
        return;
    }
    Ref<Shader> shader = SHADER_RESOURCE_MANAGER.load(asset->shader);
    if (!shader) {
        Log::error("MaterialResourceManager",
                   "Failed to reload shader for material: %s",
                   path->string().c_str());
        return;
    }
    material->rebuildFromAsset(*asset, shader);
}

void MaterialResourceManager::refreshAsset(const VirtualPath& materialPath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(materialPath);
    if (assetId)
        refreshAsset(*assetId);
}

void MaterialResourceManager::setShader(const Ref<Material>& material,
                                        const VirtualPath& shaderPath) {
    if (!material) {
        Log::error("MaterialResourceManager", "Cannot set Shader on an invalid Material");
        return;
    }
    Ref<Shader> shader = SHADER_RESOURCE_MANAGER.load(shaderPath);
    if (!shader) {
        Log::error("MaterialResourceManager",
                   "Shader failed to load: %s",
                   shaderPath.string().c_str());
        return;
    }
    material->setShader(std::move(shader));
}

void MaterialResourceManager::refreshShader(const AssetId& shaderAssetId) {
    resources_.forEach([&shaderAssetId](Material* material) {
        if (material && material->shaderRef() && material->shader().assetId() == shaderAssetId)
            material->setShader(material->shaderRef());
    });
}

Ref<Material> MaterialResourceManager::insert(const Ref<Material>& material) {
    if (!material || !material->shaderRef())
        return {};
    if (material->assetId_.valid()) {
        if (Ref<Material> existing = find(material->assetId_))
            return existing;
    }
    return insertUnkeyed(material);
}

Ref<Material> MaterialResourceManager::insertUnkeyed(const Ref<Material>& material) {
    if (!material || material->resourceId_ || !material->shaderRef())
        return {};
    const RID handle = resources_.insert(material.get());
    material->resourceId_ = handle;
    if (material->assetId_.valid())
        assetIndex_.insert_or_assign(material->assetId_, handle);
    return material;
}

Material* MaterialResourceManager::findRaw(RID handle) const {
    Material* const* stored = resources_.find(handle);
    return stored ? *stored : nullptr;
}

Ref<Material> MaterialResourceManager::find(RID handle) const {
    return Ref<Material>{findRaw(handle)};
}

Ref<Material> MaterialResourceManager::find(const AssetId& assetId) const {
    const auto found = assetIndex_.find(assetId);
    return found == assetIndex_.end() ? Ref<Material>{} : find(found->second);
}

Ref<Material> MaterialResourceManager::find(const VirtualPath& path) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(path);
    return assetId ? find(*assetId) : Ref<Material>{};
}

void MaterialResourceManager::unregister(Material* material) {
    if (!material || !material->resourceId_)
        return;
    const RID handle = material->resourceId_;
    if (material->assetId_.valid()) {
        const auto found = assetIndex_.find(material->assetId_);
        if (found != assetIndex_.end() && found->second == handle)
            assetIndex_.erase(found);
    }
    if (destroyObserver_)
        destroyObserver_(handle);
    material->resourceId_ = {};
    (void)resources_.release(handle);
}

void MaterialResourceManager::clear() {
    errorMaterial_.reset();
    std::vector<std::pair<RID, Material*>> remaining;
    remaining.reserve(resources_.size());
    resources_.forEachHandle(
        [&remaining](RID handle, Material* material) { remaining.emplace_back(handle, material); });
    for (const auto& [handle, material] : remaining) {
        if (!findRaw(handle))
            continue;
        if (destroyObserver_)
            destroyObserver_(handle);
        material->resourceId_ = {};
        (void)resources_.release(handle);
    }
    assetIndex_.clear();
}

} // namespace engine
