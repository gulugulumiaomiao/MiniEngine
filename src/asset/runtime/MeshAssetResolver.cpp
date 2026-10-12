#include "asset/types/MeshAsset.h"
#include "asset/runtime/MeshAssetResolver.h"
#include "asset/manager/AssetManager.h"
#include "asset/database/AssetDatabase.h"
#include "render/mesh/MeshBuilder.h"
namespace engine {
Ref<Mesh> resolveMeshReference(std::string_view reference) {
    if (reference.empty())
        return {};
    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    if (!path.valid() || path.scheme() != "assets")
        return {};
    const auto asset = ASSET_MANAGER.loadAsset<MeshAsset>(path);
    return asset ? asset->instantiate() : Ref<Mesh>{};
}
Ref<Mesh> resolveMeshReference(const AssetId& id) {
    const auto path = ASSET_DATABASE.findPath(id);
    return path ? resolveMeshReference(path->string()) : Ref<Mesh>{};
}
Ref<MeshAsset> findMeshAsset(const Mesh& mesh) {
    return ASSET_MANAGER.findMeshAsset(mesh);
}
VirtualPath meshAssetPath(const Mesh& mesh) {
    const auto asset = findMeshAsset(mesh);
    return asset ? asset->assetPath() : VirtualPath{};
}
Ref<MeshAsset> buildMeshAsset(const MeshBuildRecipe& recipe) {
    auto info = MeshBuilder::build(recipe);
    if (!info)
        return {};
    auto asset = makeRef<MeshAsset>();
    asset->data.desc = toMeshAsset(*info);
    asset->data.meshData = toMeshAsset(info->data);
    asset->data.buildRecipe = toMeshAsset(recipe);
    return asset;
}
Ref<MeshAsset> buildMeshAsset(const MeshBuildRecipeAssetData& recipe) {
    return buildMeshAsset(toRuntimeMesh(recipe));
}
} // namespace engine
