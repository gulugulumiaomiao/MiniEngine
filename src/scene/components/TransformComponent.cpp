#include "scene/components/TransformComponent.h"

#include "core/serialization/Transfer.h"
#include "scene/scene/Scene.h"

namespace engine {

bool TransformComponentAsset::transfer(Transfer& archive) {
    return archive.transfer("position", position) && archive.transfer("rotation", rotation) &&
           archive.transfer("scale", scale);
}

void TransformComponent::setLocalPosition(const math::Vec3& position) {
    if (localPosition_ == position)
        return;
    localPosition_ = position;
    markLocalDirty();
}

void TransformComponent::setLocalRotation(const math::Quat& rotation) {
    const math::Quat normalized = math::normalize(rotation);
    if (localRotation_ == normalized)
        return;
    localRotation_ = normalized;
    markLocalDirty();
}

void TransformComponent::setLocalScale(const math::Vec3& scale) {
    if (localScale_ == scale)
        return;
    localScale_ = scale;
    markLocalDirty();
}

const math::Mat44& TransformComponent::localMatrix() const {
    if (localDirty_) {
        localMatrix_ = math::trs(localPosition_, localRotation_, localScale_);
        localDirty_ = false;
    }
    return localMatrix_;
}

const math::Mat44& TransformComponent::worldMatrix() const {
    if (worldDirty_) {
        if (const Scene* ownerScene = scene()) {
            const_cast<Scene*>(ownerScene)->updateTransforms();
        }
    }
    return worldMatrix_;
}

math::Vec3 TransformComponent::worldPosition() const {
    const math::Mat44& matrix = worldMatrix();
    return {matrix[3][0], matrix[3][1], matrix[3][2]};
}

void TransformComponent::markLocalDirty() {
    localDirty_ = true;
    worldDirty_ = true;
    if (Scene* ownerScene = scene()) {
        if (Node* ownerNode = ownerScene->findNode(owner())) {
            ownerNode->markTransformDirty();
        }
    }
}

bool TransformComponent::updateWorld(const math::Mat44& parentWorld, bool parentChanged) {
    const bool changed = localDirty_ || worldDirty_ || parentChanged;
    if (!changed)
        return false;
    worldMatrix_ = parentWorld * localMatrix();
    worldDirty_ = false;
    return true;
}

} // namespace engine
