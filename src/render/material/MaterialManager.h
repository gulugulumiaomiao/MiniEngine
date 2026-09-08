#pragma once

#include "core/base/InstanceManager.h"
#include "core/base/Singleton.h"
#include "render/material/Material.h"

namespace engine {

class MaterialManager final : public Singleton<MaterialManager>,
                              public InstanceManager<Material, MaterialHandle> {
public:
    [[nodiscard]] MaterialHandle load(const VirtualPath& materialAssetPath) override;
    void setShader(MaterialHandle handle, const VirtualPath& shaderPath);
    void refreshShader(const VirtualPath& shaderPath);

private:
    friend class Singleton<MaterialManager>;
    MaterialManager() = default;

    [[nodiscard]] const VirtualPath& pathOf(const Material& material) const override {
        return material.assetPath();
    }
    [[nodiscard]] bool validate(const Material& material) const override;
};

} // namespace engine

#define MATERIAL_MANAGER (::engine::MaterialManager::instance())
