#include "scene/node/Node.h"

#include "core/serialization/Transfer.h"
#include "scene/scene/Scene.h"
#include "scene/components/TransformComponent.h"

namespace engine {

bool SceneNodeAsset::transfer(Transfer& archive) {
    return archive.transfer("id", id) && archive.transfer("parent", parent) &&
           archive.transfer("name", name) && archive.transfer("active", active) &&
           archive.transfer("components", components);
}

void Node::initialize(NodeHandle handle) {
    handle_ = handle;
    auto transform = std::make_unique<TransformComponent>();
    transform_ = transform.get();
    components_.push_back(std::move(transform));
    transform_->attach(*scene_, handle_, activeInHierarchy_);
}

void Node::setActive(bool active) {
    if (activeSelf_ == active)
        return;
    activeSelf_ = active;
    const Node* parent = scene_->findNode(parent_);
    refreshActiveSubtree(!parent || parent->activeInHierarchy_);
}

bool Node::setParent(NodeHandle parentHandle) {
    if (handle_ == scene_->rootHandle()) {
        Log::warn("Node", "Scene root cannot be reparented");
        return false;
    }
    if (!parentHandle)
        parentHandle = scene_->rootHandle();
    Node* parent = scene_->findNode(parentHandle);
    if (!parent) {
        Log::warn("Node", "Cannot parent node %s to an invalid node", name_.c_str());
        return false;
    }
    if (parentHandle == handle_ || wouldCreateCycle(parentHandle)) {
        Log::warn("Node", "Cannot create a cyclic node hierarchy");
        return false;
    }
    if (parent_ == parentHandle)
        return true;

    if (Node* previousParent = scene_->findNode(parent_)) {
        std::erase(previousParent->children_, handle_);
    }
    parent_ = parentHandle;
    parent->children_.push_back(handle_);
    markTransformDirty();
    refreshActiveSubtree(parent->activeInHierarchy_);
    return true;
}

bool Node::setParent(Node& parent) {
    if (scene_ != parent.scene_) {
        Log::warn("Node", "Cannot parent nodes from different scenes");
        return false;
    }
    return setParent(parent.handle_);
}

void Node::markTransformDirty() {
    transform_->markWorldDirty();
    for (NodeHandle child : children_) {
        if (Node* childNode = scene_->findNode(child)) {
            childNode->markTransformDirty();
        }
    }
}

TransformComponent& Node::transform() {
    return *transform_;
}

const TransformComponent& Node::transform() const {
    return *transform_;
}

void Node::detachComponents() {
    for (auto iterator = components_.rbegin(); iterator != components_.rend(); ++iterator) {
        (*iterator)->detach();
    }
    components_.clear();
    transform_ = nullptr;
}

void Node::refreshActiveSubtree(bool parentActive) {
    activeInHierarchy_ = activeSelf_ && parentActive;
    for (const std::unique_ptr<Component>& component : components_) {
        component->setOwnerActive(activeInHierarchy_);
    }
    for (NodeHandle child : children_) {
        if (Node* childNode = scene_->findNode(child)) {
            childNode->refreshActiveSubtree(activeInHierarchy_);
        }
    }
}

void Node::updateComponentsSubtree(float deltaTime) {
    for (const std::unique_ptr<Component>& component : components_) {
        component->update(deltaTime);
    }
    for (NodeHandle child : children_) {
        if (Node* childNode = scene_->findNode(child)) {
            childNode->updateComponentsSubtree(deltaTime);
        }
    }
}

void Node::updateTransformSubtree(const math::Mat44& parentWorld, bool parentChanged) {
    const bool changed = transform_->updateWorld(parentWorld, parentChanged);
    const math::Mat44& world = transform_->worldMatrix_;
    for (NodeHandle child : children_) {
        if (Node* childNode = scene_->findNode(child)) {
            childNode->updateTransformSubtree(world, changed);
        }
    }
}

bool Node::wouldCreateCycle(NodeHandle parentHandle) const {
    const Node* candidate = scene_->findNode(parentHandle);
    while (candidate) {
        if (candidate->handle_ == handle_)
            return true;
        candidate = scene_->findNode(candidate->parent_);
    }
    return false;
}

} // namespace engine
