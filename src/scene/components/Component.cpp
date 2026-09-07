#include "scene/components/Component.h"

namespace engine {

void Component::setEnabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    const bool next = enabled_ && scene_ && ownerActive_;
    if (next == active_)
        return;
    active_ = next;
    if (active_)
        onEnable();
    else
        onDisable();
}

void Component::attach(Scene& scene, NodeHandle owner, bool ownerActive) {
    scene_ = &scene;
    owner_ = owner;
    ownerActive_ = ownerActive;
    onAttach();
    active_ = enabled_ && ownerActive_;
    if (active_)
        onEnable();
}

void Component::detach() {
    if (!scene_)
        return;
    if (active_)
        onDisable();
    active_ = false;
    onDetach();
    owner_ = {};
    scene_ = nullptr;
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
