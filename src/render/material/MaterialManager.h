#pragma once

#include "asset/base/AssetId.h"
#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/material/Material.h"

namespace engine {

class MaterialManager final
    : public Singleton<MaterialManager>,
      public KeyedHandleRegistry<Material, MaterialHandle, AssetId> {
public:
    [[nodiscard]] MaterialHandle load(const AssetId& assetId);
    [[nodiscard]] MaterialHandle load(const VirtualPath& materialAssetPath);
    [[nodiscard]] MaterialHandle errorMaterial();
    [[nodiscard]] MaterialHandle clone(MaterialHandle source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& materialPath);
    void setShader(MaterialHandle handle, const VirtualPath& shaderPath);
    void refreshShader(const VirtualPath& shaderPath);

private:
    friend class Singleton<MaterialManager>;
    MaterialManager() = default;

    [[nodiscard]] MaterialHandle loadFromPath(const VirtualPath& materialPath,
                                              const AssetId& assetId);
    [[nodiscard]] AssetId keyOf(const Material& material) const override {
        return material.assetId();
    }
    [[nodiscard]] bool validate(const Material& material) const override;
};

} // namespace engine

#define MATERIAL_MANAGER (::engine::MaterialManager::instance())
