#pragma once

#include "scene/node/SceneHandles.h"

namespace engine {

class Node;
class Scene;

class Component {
public:
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    [[nodiscard]] NodeHandle owner() const { return owner_; }
    [[nodiscard]] Scene* scene() { return scene_; }
    [[nodiscard]] const Scene* scene() const { return scene_; }
    [[nodiscard]] bool enabled() const { return enabled_; }
    [[nodiscard]] bool active() const { return active_; }
    void setEnabled(bool enabled);

protected:
    Component() = default;

    virtual void onAttach() {}
    virtual void onDetach() {}
    virtual void onEnable() {}
    virtual void onDisable() {}
    virtual void onUpdate(float deltaTime) { (void)deltaTime; }

private:
    friend class Node;

    void attach(Scene& scene, NodeHandle owner, bool ownerActive);
    void detach();
    void setOwnerActive(bool ownerActive);
    void update(float deltaTime);

    Scene* scene_{};
    NodeHandle owner_;
    bool enabled_{true};
    bool ownerActive_{};
    bool active_{};
};

} // namespace engine
