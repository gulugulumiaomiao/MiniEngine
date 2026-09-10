#include "render/material/MaterialManager.h"

#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "render/shader/ShaderManager.h"

namespace engine {
namespace {

const VirtualPath kErrorMaterialPath{"asset://materials/error.material.json"};

} // namespace

MaterialHandle MaterialManager::load(const VirtualPath& materialPath) {
    if (!materialPath.valid()) {
        Log::error("MaterialManager", "Invalid Material path: %s", materialPath.string().c_str());
        return errorMaterial();
    }
    if (const MaterialHandle existing = findHandle(materialPath); existing) {
        return existing;
    }
    Log::info("Material", "Loading material: %s", materialPath.string().c_str());
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    if (!asset) {
        return materialPath == kErrorMaterialPath ? MaterialHandle{} : errorMaterial();
    }
    const ShaderHandle shader = SHADER_MANAGER.load(asset->shader);
    if (!shader)
        return materialPath == kErrorMaterialPath ? MaterialHandle{} : errorMaterial();
    return insert(asset->instantiate(shader));
}

MaterialHandle MaterialManager::errorMaterial() {
    if (const MaterialHandle existing = findHandle(kErrorMaterialPath); existing)
        return existing;

    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(kErrorMaterialPath);
    const ShaderHandle shader = SHADER_MANAGER.builtinColor();
    if (!asset || !shader) {
        Log::error("MaterialManager", "Built-in Error Material is unavailable");
        return {};
    }
    return insert(asset->instantiate(shader));
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
