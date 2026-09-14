#pragma once

#include "scene/node/SceneHandles.h"
#include "tools/editor/SelectionSet.h"

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

    // 单选兼容视图：空集返回无效句柄，否则返回 primary（首个选中项）。
    [[nodiscard]] NodeHandle selection() const {
        return selection_.empty() ? NodeHandle{} : selection_.primary();
    }
    [[nodiscard]] const SelectionSet<NodeHandle>& selectionSet() const { return selection_; }
    void select(NodeHandle handle) {
        if (handle)
            selection_.select(handle);
        else
            selection_.clear();
    }

private:
    enum class Action { Create, Delete, Move, SetActive };
    enum class Drop { None, Before, Into, After };
    struct Request {
        Action action;
        std::uint64_t revision;
        // Delete/SetActive 作用于整个集合；Move 只使用首个元素。
        std::vector<NodeHandle> nodes;
        NodeHandle parent{};
        std::size_t index{};
        bool active{};
    };

    void drawNode(NodeHandle handle);
    void drawContextMenu(NodeHandle handle);
    Drop drawDropTarget(NodeHandle handle, const ImVec2& min, const ImVec2& max, bool rootTarget);
    void drawRename(NodeHandle handle);
    void beginRename(NodeHandle handle);
    void expandAncestors(NodeHandle handle);
    void applyRequest();
    void applyDelete(const std::vector<NodeHandle>& nodes);
    void applySetActive(const std::vector<NodeHandle>& nodes, bool active);

    SceneDocument& document_;
    std::uint64_t revision_{};
    SelectionSet<NodeHandle> selection_;
    // 按下节点行时暂存的潜在拖动组；拖动源激活时整组随行。
    SelectionSet<NodeHandle> dragStartSelection_;
    // 本帧可见行顺序，供 Shift 范围选择使用。
    std::vector<NodeHandle> visibleNodes_;
    NodeHandle renameTarget_;
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
