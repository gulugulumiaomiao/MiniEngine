#include "render/material/MaterialManager.h"

#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "render/shader/ShaderManager.h"

namespace engine {

MaterialHandle MaterialManager::load(const VirtualPath& materialPath) {
    if (!materialPath.valid()) {
        Log::error("MaterialManager", "Invalid Material path: %s", materialPath.string().c_str());
        return {};
    }
    if (const MaterialHandle existing = handleFor(materialPath); existing) {
        return existing;
    }
    Log::info("Material", "Loading material: %s", materialPath.string().c_str());
    const std::shared_ptr<MaterialAsset> asset =
        ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    if (!asset) {
        return {};
    }
    const ShaderHandle shader = SHADER_MANAGER.load(asset->shader);
    if (!shader)
        return {};
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
