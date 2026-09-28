#include "scene/components/MeshComponent.h"

#include "core/serialization/Transfer.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshBuilder.h"

#include <utility>

namespace engine {

bool MeshComponentAsset::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("source_type", sourceType))
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
           archive.transfer("layer_mask", layerMask) && archive.endObject();
}

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
    primitiveDirty_ = false;
    // 运行时图元：经 MeshBuilder 产出临时 MeshAsset 再 instantiate（取代已删除的
    // MeshResourceManager::createRuntime/rebuildRuntime）。重建即替换：旧 mesh_ 的最后一个
    // Ref 归零时，~Mesh 自动释放其 GPU 缓冲。临时 asset 随即析构并清除观察者回指，
    // 得到的 Mesh 脱离 asset（等价 clone）。
    const Ref<MeshAsset> asset = MeshBuilder::buildAsset(*primitiveRecipe_);
    Ref<Mesh> built = asset ? asset->instantiate() : Ref<Mesh>{};
    if (!built)
        return false;
    mesh_ = std::move(built);
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
