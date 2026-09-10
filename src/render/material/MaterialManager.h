#pragma once

#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/material/Material.h"

namespace engine {

class MaterialManager final
    : public Singleton<MaterialManager>,
      public KeyedHandleRegistry<Material, MaterialHandle, VirtualPath, VirtualPathHash> {
public:
    [[nodiscard]] MaterialHandle load(const VirtualPath& materialAssetPath);
    [[nodiscard]] MaterialHandle errorMaterial();
    void setShader(MaterialHandle handle, const VirtualPath& shaderPath);
    void refreshShader(const VirtualPath& shaderPath);

private:
    friend class Singleton<MaterialManager>;
    MaterialManager() = default;

    [[nodiscard]] VirtualPath keyOf(const Material& material) const override {
        return material.assetPath();
    }
    [[nodiscard]] bool validate(const Material& material) const override;
};

} // namespace engine

#define MATERIAL_MANAGER (::engine::MaterialManager::instance())
