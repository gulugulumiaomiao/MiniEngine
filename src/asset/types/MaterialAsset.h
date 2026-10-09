#pragma once
#include "asset/base/Asset.h"
#include "asset/types/MaterialAssetData.h"
#include "render/material/Material.h"
namespace engine {
class MaterialAsset final : public Asset {
public:
    AssetType type() const override { return AssetType::Material; }
    MaterialAssetData data;
    Ref<Material> instantiate();
    Ref<Material> clone() const;
    const Ref<Material>& runtimeInstance() const { return instance_; }
    std::optional<MaterialCreateInfo>
    buildRuntimeInfo(const Ref<Shader>& overrideShader = {}) const;
    bool transfer(Transfer& archive) override;

private:
    Ref<Material> instance_;
};
} // namespace engine
