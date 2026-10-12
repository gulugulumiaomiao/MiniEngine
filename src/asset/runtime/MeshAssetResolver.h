#pragma once
#include "asset/types/MeshAsset.h"
#include "asset/runtime/MeshAssetConversion.h"
namespace engine {
Ref<Mesh> resolveMeshReference(std::string_view reference);
Ref<Mesh> resolveMeshReference(const AssetId& id);
Ref<MeshAsset> findMeshAsset(const Mesh& mesh);
VirtualPath meshAssetPath(const Mesh& mesh);
Ref<MeshAsset> buildMeshAsset(const MeshBuildRecipe& recipe);
Ref<MeshAsset> buildMeshAsset(const MeshBuildRecipeAssetData& recipe);
} // namespace engine
