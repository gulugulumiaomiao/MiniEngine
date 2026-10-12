#include "scene/components/MeshComponent.h"

#include "render/mesh/Mesh.h"
#include "render/mesh/MeshBuilder.h"

#include <utility>

namespace engine {

void MeshComponent::setAssetMesh(Ref<Mesh> mesh) {
    releaseOwnedMesh();
    sourceType_ = MeshComponentSourceType::Asset;
    mesh_ = std::move(mesh);
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
    auto info = MeshBuilder::build(*primitiveRecipe_);
    if (!info)
        return false;
    if (ownsRuntimeMesh_ && mesh_) {
        if (!mesh_->rebuild(std::move(*info)))
            return false;
    } else {
        auto built = Mesh::create(std::move(*info));
        if (!built)
            return false;
        mesh_ = std::move(built);
    }
    primitiveDirty_ = false;
    ownsRuntimeMesh_ = true;
    return true;
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
    mesh_.reset();
    ownsRuntimeMesh_ = false;
}

} // namespace engine
