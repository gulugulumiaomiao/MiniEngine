#pragma once

#include "scene/node/SceneHandles.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ImVec2;

namespace engine::editor {

class SceneDocument;

// Scene tree panel: selection, rename, active toggle, create/delete nodes and basic
// hierarchy operations through the existing Scene/Node API.
class HierarchyPanel {
public:
    explicit HierarchyPanel(SceneDocument& document) : document_(document) {}

    void draw();
    // 隐藏面板时也同步，避免 Inspector 继续使用旧场景的选择。
    void syncDocument();

    [[nodiscard]] NodeHandle selection() const { return selection_; }
    void select(NodeHandle handle) { selection_ = handle; }

private:
    enum class Action { Create, Delete, Move };
    enum class Drop { None, Before, Into, After };
    struct Request {
        Action action;
        std::uint64_t revision;
        NodeHandle node{};
        NodeHandle parent{};
        std::size_t index{};
    };

    void drawNode(NodeHandle handle);
    void drawContextMenu(NodeHandle handle);
    Drop drawDropTarget(NodeHandle handle, const ImVec2& min, const ImVec2& max, bool rootTarget);
    void drawRename(NodeHandle handle);
    void beginRename(NodeHandle handle);
    void expandAncestors(NodeHandle handle);
    void applyRequest();

    SceneDocument& document_;
    std::uint64_t revision_{};
    NodeHandle selection_{};
    NodeHandle renameTarget_{};
    std::vector<char> renameBuffer_;
    bool focusRename_{};
    std::optional<Request> pending_;
    std::vector<NodeHandle> expand_;
    NodeHandle hoverTarget_{};
    double hoverSince_{};
    bool hoverSeen_{};
    std::string statusMessage_;
};

} // namespace engine::editor
