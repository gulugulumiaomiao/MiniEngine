#pragma once

#include "asset/base/AssetId.h"
#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/material/Material.h"

namespace engine {

class MaterialManager final
    : public Singleton<MaterialManager>,
      public KeyedHandleRegistry<Material, RID, AssetId> {
public:
    [[nodiscard]] RID load(const AssetId& assetId);
    [[nodiscard]] RID load(const VirtualPath& materialAssetPath);
    [[nodiscard]] RID errorMaterial();
    [[nodiscard]] RID clone(RID source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& materialPath);
    void setShader(RID handle, const VirtualPath& shaderPath);
    void refreshShader(const AssetId& shaderAssetId);

private:
    friend class Singleton<MaterialManager>;
    MaterialManager() = default;

    [[nodiscard]] RID loadFromPath(const VirtualPath& materialPath,
                                              const AssetId& assetId);
    [[nodiscard]] AssetId keyOf(const Material& material) const override {
        return material.assetId();
    }
    [[nodiscard]] bool validate(const Material& material) const override;
};

} // namespace engine

#define MATERIAL_MANAGER (::engine::MaterialManager::instance())
