#pragma once
#include "asset/types/MaterialAsset.h"
#include "asset/base/AssetId.h"
namespace engine {
Ref<Material> resolveMaterialReference(const VirtualPath& path);
Ref<Material> resolveMaterialReference(const AssetId& id);
Ref<MaterialAsset> findMaterialAsset(const Material& material);
VirtualPath materialAssetPath(const Material& material);
bool initializeErrorMaterial();
void setMaterialShader(const Ref<Material>& material, const VirtualPath& shaderPath);
MaterialTextureBinding materialTextureBinding(std::string reference);
std::string materialTextureReference(const Material& material, std::string_view name);
void setMaterialTextureReference(Material& material, std::string_view name, std::string reference);
} // namespace engine
