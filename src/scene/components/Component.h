#pragma once

#include "core/base/RefCounted.h"
#include "scene/node/SceneHandles.h"

namespace engine {

class Node;
class Scene;

class Component : public RefCounted {
public:
    ~Component() override = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    [[nodiscard]] RID owner() const;
    [[nodiscard]] Node* node() { return node_; }
    [[nodiscard]] const Node* node() const { return node_; }
    [[nodiscard]] Scene* scene();
    [[nodiscard]] const Scene* scene() const;
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

    void attach(Node& node, bool ownerActive);
    void detach();
    void setOwnerActive(bool ownerActive);
    void update(float deltaTime);

    Node* node_{};
    bool enabled_{true};
    bool ownerActive_{};
    bool active_{};
};

} // namespace engine
