#pragma once

#include "core/logging/Log.h"
#include "core/serialization/Transferable.h"
#include "core/math/Math.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/Component.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace engine {

class Scene;

using SceneNodeAssetId = std::uint32_t;

using SceneComponentAsset = std::variant<TransformComponentAsset,
                                         MeshComponentAsset,
                                         MaterialComponentAsset,
                                         CameraComponentAsset,
                                         LightComponentAsset>;

struct SceneNodeAsset final : public Transferable {
    SceneNodeAsset() = default;
    SceneNodeAsset(SceneNodeAssetId id,
                   std::optional<SceneNodeAssetId> parent,
                   std::string name,
                   bool active,
                   std::vector<SceneComponentAsset> components)
        : id(id), parent(parent), name(std::move(name)), active(active),
          components(std::move(components)) {}

    SceneNodeAssetId id{};
    std::optional<SceneNodeAssetId> parent;
    std::string name{"Node"};
    bool active{true};
    std::vector<SceneComponentAsset> components;

    bool operator==(const SceneNodeAsset&) const = default;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class Node final {
public:
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) noexcept = default;
    Node& operator=(Node&&) noexcept = default;
    ~Node() = default;

    [[nodiscard]] NodeHandle handle() const { return handle_; }
    [[nodiscard]] Scene& scene() { return *scene_; }
    [[nodiscard]] const Scene& scene() const { return *scene_; }
    [[nodiscard]] std::string_view name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    [[nodiscard]] NodeHandle parent() const { return parent_; }
    [[nodiscard]] const std::vector<NodeHandle>& children() const { return children_; }

    [[nodiscard]] bool activeSelf() const { return activeSelf_; }
    [[nodiscard]] bool activeInHierarchy() const { return activeInHierarchy_; }
    void setActive(bool active);
    [[nodiscard]] bool setParent(NodeHandle parent = {});
    [[nodiscard]] bool setParent(Node& parent);
    void markTransformDirty();

    [[nodiscard]] TransformComponent& transform();
    [[nodiscard]] const TransformComponent& transform() const;

    template <std::derived_from<Component> T, typename... Args> T* addComponent(Args&&... args) {
        if (T* existing = getComponent<T>()) {
            Log::warn("Node", "Node %s already has component", name_.c_str());
            return existing;
        }
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = component.get();
        components_.push_back(std::move(component));
        result->attach(*this, activeInHierarchy_);
        return result;
    }

    template <std::derived_from<Component> T> [[nodiscard]] T* getComponent() {
        for (const std::unique_ptr<Component>& component : components_) {
            if (auto* result = dynamic_cast<T*>(component.get()))
                return result;
        }
        return nullptr;
    }

    template <std::derived_from<Component> T> [[nodiscard]] const T* getComponent() const {
        for (const std::unique_ptr<Component>& component : components_) {
            if (const auto* result = dynamic_cast<const T*>(component.get())) {
                return result;
            }
        }
        return nullptr;
    }

    template <std::derived_from<Component> T> bool removeComponent() {
        if constexpr (std::is_same_v<T, TransformComponent>) {
            Log::warn("Node", "TransformComponent cannot be removed");
            return false;
        }
        const auto found =
            std::ranges::find_if(components_, [](const std::unique_ptr<Component>& component) {
                return dynamic_cast<T*>(component.get()) != nullptr;
            });
        if (found == components_.end())
            return false;
        (*found)->detach();
        components_.erase(found);
        return true;
    }

private:
    friend class Scene;

    explicit Node(Scene& scene, std::string name) : scene_(&scene), name_(std::move(name)) {}

    void initialize(NodeHandle handle);
    void detachComponents();
    void refreshActiveSubtree(bool parentActive);
    void updateComponentsSubtree(float deltaTime);
    void updateTransformSubtree(const math::Mat44& parentWorld, bool parentChanged);
    [[nodiscard]] bool wouldCreateCycle(NodeHandle parent) const;

    Scene* scene_{};
    NodeHandle handle_;
    std::string name_;
    NodeHandle parent_;
    std::vector<NodeHandle> children_;
    std::vector<std::unique_ptr<Component>> components_;
    TransformComponent* transform_{};
    bool activeSelf_{true};
    bool activeInHierarchy_{true};
};

} // namespace engine
