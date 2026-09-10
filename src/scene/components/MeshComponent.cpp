#include "scene/components/MeshComponent.h"

#include "core/serialization/Transfer.h"
#include "render/mesh/MeshManager.h"

#include <utility>

namespace engine {

bool MeshComponentAsset::transfer(Transfer& archive) {
    if (!archive.transfer("source_type", sourceType))
        return false;
    if (sourceType == MeshComponentSourceType::Asset) {
        if (!archive.transfer("mesh", mesh))
            return false;
    } else if (sourceType == MeshComponentSourceType::Primitive) {
        if (!archive.transfer("primitive_recipe", primitiveRecipe))
            return false;
    } else {
        return false;
    }
    return archive.transfer("enabled", enabled) && archive.transfer("visible", visible) &&
           archive.transfer("cast_shadow", castShadow) &&
           archive.transfer("receive_shadow", receiveShadow) &&
           archive.transfer("layer_mask", layerMask);
}

void MeshComponent::setAssetMesh(MeshHandle mesh) {
    releaseOwnedMesh();
    sourceType_ = MeshComponentSourceType::Asset;
    mesh_ = mesh;
    primitiveRecipe_.reset();
    primitiveDirty_ = false;
}

void MeshComponent::setPrimitive(const PlaneGeometry& geometry) {
    setSinglePrimitive(geometry);
}

void MeshComponent::setPrimitive(const BoxGeometry& geometry) {
    setSinglePrimitive(geometry);
}

void MeshComponent::setPrimitive(const UvSphereGeometry& geometry) {
    setSinglePrimitive(geometry);
}

void MeshComponent::setPrimitive(const CylinderGeometry& geometry) {
    setSinglePrimitive(geometry);
}

void MeshComponent::setPrimitiveRecipe(MeshBuildRecipe recipe) {
    if (!ownsRuntimeMesh_)
        mesh_ = {};
    sourceType_ = MeshComponentSourceType::Primitive;
    primitiveRecipe_ = std::move(recipe);
    primitiveDirty_ = true;
}

bool MeshComponent::applyPrimitiveChanges() {
    if (sourceType_ != MeshComponentSourceType::Primitive || !primitiveRecipe_)
        return false;
    if (!primitiveDirty_)
        return static_cast<bool>(mesh_);
    primitiveDirty_ = false;
    if (ownsRuntimeMesh_ && mesh_)
        return MESH_MANAGER.rebuildRuntime(mesh_, *primitiveRecipe_);

    mesh_ = MESH_MANAGER.createRuntime(*primitiveRecipe_);
    ownsRuntimeMesh_ = static_cast<bool>(mesh_);
    return ownsRuntimeMesh_;
}

void MeshComponent::onAttach() {
    if (primitiveDirty_)
        (void)applyPrimitiveChanges();
}

void MeshComponent::onDetach() {
    releaseOwnedMesh();
}

void MeshComponent::onEnable() {
    if (primitiveDirty_)
        (void)applyPrimitiveChanges();
}

void MeshComponent::onUpdate(float deltaTime) {
    (void)deltaTime;
    if (primitiveDirty_)
        (void)applyPrimitiveChanges();
}

template <typename Geometry> void MeshComponent::setSinglePrimitive(const Geometry& geometry) {
    MeshBuildRecipe recipe;
    recipe.name = "Runtime Primitive";
    recipe.parts.emplace_back(geometry);
    recipe.usage = MeshUsage::Dynamic;
    recipe.keepCpuCopy = true;
    setPrimitiveRecipe(std::move(recipe));
}

void MeshComponent::releaseOwnedMesh() {
    if (!ownsRuntimeMesh_ || !mesh_)
        return;
    (void)MESH_MANAGER.destroyRuntime(mesh_);
    mesh_ = {};
    ownsRuntimeMesh_ = false;
}

} // namespace engine
