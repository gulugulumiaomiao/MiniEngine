#include "asset/types/TextureAsset.h"
#include "asset/runtime/MaterialAssetResolver.h"
#include "asset/runtime/ShaderAssetResolver.h"
#include "asset/manager/AssetManager.h"
#include "asset/database/AssetDatabase.h"
#include "core/logging/Log.h"
namespace engine {
namespace {
struct TextureReference {
    std::string reference;
    Ref<Texture> operator()() const { return resolveTextureReference(reference); }
};
} // namespace
MaterialTextureBinding materialTextureBinding(std::string reference) {
    MaterialTextureBinding binding;
    binding.resolve = TextureReference{std::move(reference)};
    return binding;
}
std::string materialTextureReference(const Material& material, std::string_view name) {
    material.ensureShaderCurrent();
    const auto found = material.textures.find(std::string{name});
    if (found == material.textures.end())
        return {};
    const auto& binding = found->second;
    if (const auto* source = binding.resolve.target<TextureReference>())
        return source->reference;
    return binding.texture && binding.texture->isAssetBacked()
               ? binding.texture->assetPath().string()
               : std::string{};
}
void setMaterialTextureReference(Material& material, std::string_view name, std::string reference) {
    material.setTexture(name, materialTextureBinding(std::move(reference)));
}
Ref<Material> resolveMaterialReference(const VirtualPath& path) {
    const auto asset = ASSET_MANAGER.loadAsset<MaterialAsset>(path);
    const auto material = asset ? asset->instantiate() : Ref<Material>{};
    return material ? material : Material::errorMaterial;
}
Ref<Material> resolveMaterialReference(const AssetId& id) {
    const auto path = ASSET_DATABASE.findPath(id);
    return path ? resolveMaterialReference(*path) : Material::errorMaterial;
}
bool initializeErrorMaterial() {
    Material::errorMaterial.reset();
    const auto asset = ASSET_MANAGER.loadAsset<MaterialAsset>(
        VirtualPath{"assets://materials/error.material.json"});
    Material::errorMaterial = asset ? asset->instantiate() : Ref<Material>{};
    return static_cast<bool>(Material::errorMaterial);
}
Ref<MaterialAsset> findMaterialAsset(const Material& material) {
    return ASSET_MANAGER.findMaterialAsset(material);
}
VirtualPath materialAssetPath(const Material& material) {
    const auto asset = findMaterialAsset(material);
    return asset ? asset->assetPath() : VirtualPath{};
}
void setMaterialShader(const Ref<Material>& material, const VirtualPath& shaderPath) {
    if (material) {
        if (auto shader = resolveShaderReference(shaderPath.string()))
            material->setShader(std::move(shader));
    }
}
} // namespace engine
