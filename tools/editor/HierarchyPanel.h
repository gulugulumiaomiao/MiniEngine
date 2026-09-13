#pragma once

#include "scene/node/SceneHandles.h"

namespace engine::editor {

class SceneDocument;

// Scene tree panel: selection, rename, active toggle, create/delete nodes and basic
// hierarchy operations through the existing Scene/Node API.
class HierarchyPanel {
public:
    explicit HierarchyPanel(SceneDocument& document) : document_(document) {}

    void draw();

    [[nodiscard]] NodeHandle selection() const { return selection_; }
    void select(NodeHandle handle) { selection_ = handle; }

private:
    void drawNode(NodeHandle handle, NodeHandle parentOfRootItems);
    bool beginTreeNode(const char* name, bool& selected, bool& opened);

    SceneDocument& document_;
    NodeHandle selection_{};
    NodeHandle renameTarget_{};
    char renameBuffer_[128]{};
};

} // namespace engine::editor
