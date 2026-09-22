#pragma once

#include "core/filesystem/VirtualPath.h"
#include "scene/node/SceneHandles.h"
#include "tools/editor/widgets/MaterialInspector.h"
#include "tools/editor/model/SelectionSet.h"

#include <optional>
#include <string>
#include <vector>

namespace engine {

class MeshComponent;
class Node;

namespace editor {

class SceneDocument;

// Inspector for the selected node: name, active flag and per-component editors for
// Transform, Mesh, Material, Camera and Light using the existing runtime API. With a
// multi-selection it shows only the shared fields and edits apply to every node.
// Node view and the Project-window asset view (materials get the live editor, other
// types a read-only summary) follow a last-interaction-wins policy: whichever of
// the Hierarchy or the Project window the user clicked last owns the panel, and the
// other selection waits until it is clicked again.
class InspectorPanel {
public:
    explicit InspectorPanel(SceneDocument& document) : document_(document) {}

    // Which window the panel is currently serving. EditorApplication switches
    // the mode on selection stamps, never on values: re-clicking the same node
    // or asset must also reclaim the panel.
    enum class InspectionMode { Nodes, Asset };

    void draw(const SelectionSet<RID>& selection);

    // Project-window asset inspection (EditorApplication bridges the Project
    // selection in). draw() re-validates the path, so a deleted or renamed
    // asset degrades to a message instead of editing stale state.
    void inspectAsset(const VirtualPath& path) {
        assetSelection_ = path;
        mode_ = InspectionMode::Asset;
    }
    // Drops the asset target and hands the panel back to the node view; used
    // when the Project selection clears (directories count as cleared) and on
    // project close/open, where no stale inspection may survive.
    void clearAssetInspection() {
        assetSelection_.reset();
        mode_ = InspectionMode::Nodes;
    }
    // The Hierarchy window was interacted with (node pick, blank-click clear or
    // an operation that focuses a node): the node view takes over, even with an
    // asset still selected in Project.
    void focusNodeSelection() { mode_ = InspectionMode::Nodes; }
    // Material edits are debounced to disk; flush them on shutdown/project
    // switch so the last change is never lost.
    void flushMaterialSaves() { materialInspector_.flushPendingSave(); }

private:
    void drawNodeHeader(Node& node);
    void drawMultiHeader(const SelectionSet<RID>& selection);
    void drawMultiActive(const SelectionSet<RID>& selection);
    void drawMultiTransform(const SelectionSet<RID>& selection);
    [[nodiscard]] std::vector<Node*> collectNodes(const SelectionSet<RID>& selection) const;
    void drawTransform(Node& node);
    void drawMesh(Node& node);
    void drawMaterial(Node& node);
    void drawCamera(Node& node);
    void drawLight(Node& node);
    void drawAddComponent(Node& node);
    void drawPrimitive(MeshComponent& mesh);
    void drawAssetInspector();

    // Component-editor shell, first half: null check -> CollapsingHeader
    // (DefaultOpen) -> PushID(title) -> Enabled checkbox. Returns false when
    // the whole editor must be skipped (no component or a collapsed header).
    // Defined and instantiated for the optional components in
    // InspectorPanelComponents.cpp, so this header needs no component includes.
    template <typename T> bool beginComponent(const char* title, T* component);
    // Shell second half: Separator -> Remove Component (removeComponent<T>) -> PopID.
    template <typename T> void endComponent(Node& node);

    SceneDocument& document_;
    std::string statusMessage_;
    // Last-interaction-wins target; Nodes is the resting state after clears.
    InspectionMode mode_{InspectionMode::Nodes};
    std::optional<VirtualPath> assetSelection_;
    MaterialInspector materialInspector_;
};

} // namespace editor
} // namespace engine
