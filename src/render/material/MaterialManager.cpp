#include "render/material/MaterialManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "render/shader/ShaderManager.h"

namespace engine {
namespace {

// The engine's fallback Material is part of the built-in contract layer, refreshed into
// every project's assets/ on open (syncEngineContractIntoProject), so it resolves through
// the project's assets:// mount like any user asset.
const VirtualPath kErrorMaterialPath{"assets://materials/error.material.json"};

} // namespace

MaterialHandle MaterialManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MaterialManager", "Invalid Material AssetId");
        return errorMaterial();
    }
    if (const MaterialHandle existing = findHandle(assetId); existing) {
        return existing;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("MaterialManager",
                   "Unknown Material AssetId: %s",
                   assetId.toString().c_str());
        return errorMaterial();
    }
    return loadFromPath(*path, assetId);
}

MaterialHandle MaterialManager::load(const VirtualPath& materialPath) {
    if (!materialPath.valid()) {
        Log::error("MaterialManager",
                   "Invalid Material path: %s",
                   materialPath.string().c_str());
        return errorMaterial();
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(materialPath);
    if (!assetId) {
        Log::error("MaterialManager",
                   "Material path has no AssetId: %s",
                   materialPath.string().c_str());
        return errorMaterial();
    }
    if (const MaterialHandle existing = findHandle(*assetId); existing) {
        return existing;
    }
    return loadFromPath(materialPath, *assetId);
}

MaterialHandle MaterialManager::loadFromPath(const VirtualPath& materialPath,
                                             const AssetId& assetId) {
    Log::info("Material", "Loading material: %s", materialPath.string().c_str());
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    if (!asset) {
        return materialPath == kErrorMaterialPath ? MaterialHandle{} : errorMaterial();
    }
    const ShaderHandle shader = SHADER_MANAGER.load(asset->shader);
    if (!shader) {
        return materialPath == kErrorMaterialPath ? MaterialHandle{} : errorMaterial();
    }
    Material material = asset->instantiate(shader);
    material.assetId_ = assetId;
    return insert(std::move(material));
}

MaterialHandle MaterialManager::errorMaterial() {
    const std::optional<AssetId> errorId = ASSET_DATABASE.findGuid(kErrorMaterialPath);
    if (!errorId) {
        Log::error("MaterialManager",
                   "Error Material has no AssetId; database may be uninitialized");
        return {};
    }
    if (const MaterialHandle existing = findHandle(*errorId); existing)
        return existing;
    return load(kErrorMaterialPath);
}

MaterialHandle MaterialManager::clone(MaterialHandle source) {
    Material* material = find(source);
    if (!material) {
        Log::error("MaterialManager", "Cannot clone an invalid Material");
        return {};
    }
    return insertUnkeyed(material->clone());
}

void MaterialManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MaterialManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("MaterialManager",
                   "Unknown Material AssetId: %s",
                   assetId.toString().c_str());
        return;
    }
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(*path);
    if (!asset) {
        Log::error("MaterialManager",
                   "Failed to reload material asset: %s",
                   path->string().c_str());
        return;
    }
    const ShaderHandle shader = SHADER_MANAGER.load(asset->shader);
    if (!shader) {
        Log::error("MaterialManager",
                   "Failed to reload shader for material: %s",
                   path->string().c_str());
        return;
    }
    forEach([&assetId, &asset, shader](Material& material) {
        if (material.assetId() == assetId) {
            material.rebuildFromAsset(*asset, shader);
        }
    });
}

void MaterialManager::refreshAsset(const VirtualPath& materialPath) {
    if (!materialPath.valid()) {
        Log::error("MaterialManager",
                   "Invalid Material path: %s",
                   materialPath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(materialPath);
    if (!assetId) {
        Log::error("MaterialManager",
                   "Material path has no AssetId: %s",
                   materialPath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

bool MaterialManager::validate(const Material& material) const {
    if (!SHADER_MANAGER.find(material.shaderHandle())) {
        Log::error("MaterialManager", "Material has an invalid ShaderHandle");
        return false;
    }
    return true;
}

void MaterialManager::setShader(MaterialHandle handle, const VirtualPath& shaderPath) {
    Material* material = find(handle);
    if (!material) {
        Log::error("MaterialManager", "Cannot set Shader on an invalid Material");
        return;
    }
    const ShaderHandle shader = SHADER_MANAGER.load(shaderPath);
    if (!shader) {
        Log::error("MaterialManager", "Shader failed to load: %s", shaderPath.string().c_str());
        return;
    }
    material->setShader(shader);
}

void MaterialManager::refreshShader(const VirtualPath& shaderPath) {
    forEach([&shaderPath](Material& material) {
        if (material.shader().assetPath() == shaderPath) {
            material.setShader(material.shaderHandle());
        }
    });
}

} // namespace engine
