#pragma once

#include "core/base/HandlePool.h"
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

class Scene final {
public:
    explicit Scene(std::string name = "Scene");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] std::string_view name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    [[nodiscard]] NodeHandle createNode(std::string name = "Node");
    [[nodiscard]] bool destroyNode(NodeHandle node);
    void clear();

#if defined(MINI_EDITOR)
    // finalIndex 是从目标子列表排除源节点后的插入位置，换父节点时保持世界变换。
    [[nodiscard]] bool
    canMoveNode(NodeHandle node, NodeHandle parent, std::size_t finalIndex, std::string& error);
    [[nodiscard]] NodeMoveResult
    moveNode(NodeHandle node, NodeHandle parent, std::size_t finalIndex, std::string& error);
#endif

    [[nodiscard]] Node* findNode(NodeHandle node) { return nodes_.find(node); }
    [[nodiscard]] const Node* findNode(NodeHandle node) const { return nodes_.find(node); }
    [[nodiscard]] std::size_t nodeCount() const { return nodes_.size(); }
    [[nodiscard]] NodeHandle rootHandle() const { return root_; }
    [[nodiscard]] Node& root() { return *nodes_.find(root_); }
    [[nodiscard]] const Node& root() const { return *nodes_.find(root_); }

    void update(float deltaTime);
    void updateTransforms();
    void buildRenderScene(RenderScene& output, float aspectRatio);

private:
    friend class Node;
    friend class TransformComponent;

    void extractRenderNode(Node& node, RenderScene& output, float aspectRatio);

    std::string name_;
    HandlePool<Node, NodeHandle> nodes_;
    NodeHandle root_;
};

} // namespace engine
