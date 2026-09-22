#pragma once

#include "core/base/HandlePool.h"
#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "core/math/Math.h"
#include "scene/node/Node.h"

#include <cstddef>
#include <string>
#include <utility>

namespace engine {

class RenderScene;

#if defined(MINI_EDITOR)
enum class NodeMoveResult { Rejected, Unchanged, Changed };
#endif

class Scene final : public RefCounted {
public:
    explicit Scene(std::string name = "Scene");
    ~Scene() override;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] std::string_view name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    [[nodiscard]] RID createNode(std::string name = "Node");
    [[nodiscard]] bool destroyNode(RID node);
    void clear();

#if defined(MINI_EDITOR)
    // finalIndex 是从目标子列表排除源节点后的插入位置，换父节点时保持世界变换。
    [[nodiscard]] bool
    canMoveNode(RID node, RID parent, std::size_t finalIndex, std::string& error);
    [[nodiscard]] NodeMoveResult
    moveNode(RID node, RID parent, std::size_t finalIndex, std::string& error);
#endif

    [[nodiscard]] Node* findNode(RID node) {
        Ref<Node>* value = nodes_.find(node);
        return value ? value->get() : nullptr;
    }
    [[nodiscard]] const Node* findNode(RID node) const {
        const Ref<Node>* value = nodes_.find(node);
        return value ? value->get() : nullptr;
    }
    [[nodiscard]] std::size_t nodeCount() const { return nodes_.size(); }
    [[nodiscard]] RID rootHandle() const { return root_; }
    [[nodiscard]] Node& root() { return *findNode(root_); }
    [[nodiscard]] const Node& root() const { return *findNode(root_); }

    void update(float deltaTime);
    void updateTransforms();
    void buildRenderScene(RenderScene& output, float aspectRatio);

private:
    friend class Node;
    friend class TransformComponent;

    void extractRenderNode(Node& node, RenderScene& output, float aspectRatio);

    std::string name_;
    HandlePool<Ref<Node>, RID> nodes_;
    RID root_;
};

} // namespace engine
