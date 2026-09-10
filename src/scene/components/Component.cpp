#include "scene/components/Component.h"

#include "scene/node/Node.h"

namespace engine {

NodeHandle Component::owner() const {
    return node_ ? node_->handle() : NodeHandle{};
}

Scene* Component::scene() {
    return node_ ? &node_->scene() : nullptr;
}

const Scene* Component::scene() const {
    return node_ ? &node_->scene() : nullptr;
}

void Component::setEnabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    const bool next = enabled_ && node_ && ownerActive_;
    if (next == active_)
        return;
    active_ = next;
    if (active_)
        onEnable();
    else
        onDisable();
}

void Component::attach(Node& node, bool ownerActive) {
    node_ = &node;
    ownerActive_ = ownerActive;
    onAttach();
    active_ = enabled_ && ownerActive_;
    if (active_)
        onEnable();
}

void Component::detach() {
    if (!node_)
        return;
    if (active_)
        onDisable();
    active_ = false;
    onDetach();
    node_ = nullptr;
    ownerActive_ = false;
}

void Component::setOwnerActive(bool ownerActive) {
    ownerActive_ = ownerActive;
    const bool next = enabled_ && ownerActive_;
    if (next == active_)
        return;
    active_ = next;
    if (active_)
        onEnable();
    else
        onDisable();
}

void Component::update(float deltaTime) {
    if (active_)
        onUpdate(deltaTime);
}

} // namespace engine
