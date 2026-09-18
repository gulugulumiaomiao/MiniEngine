#include "tools/editor/panels/InspectorPanel.h"

#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "scene/node/Node.h"
#include "scene/scene/Scene.h"
#include "tools/editor/model/SceneDocument.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace engine::editor {

void InspectorPanel::draw(const SelectionSet<NodeHandle>& selection) {
    if (!ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    statusMessage_.clear();
    if (!document_.valid()) {
        ImGui::TextUnformatted("No Scene document is open");
        ImGui::End();
        return;
    }

    // Last-interaction-wins: the panel serves the window the user clicked
    // last (EditorApplication switches the mode on selection stamps), so a
    // Project asset pick shows the asset editor even with nodes selected,
    // and any Hierarchy interaction switches back to the node view.
    if (mode_ == InspectionMode::Asset) {
        if (assetSelection_.has_value())
            drawAssetInspector();
        else
            ImGui::TextUnformatted("Nothing selected");
        ImGui::End();
        return;
    }

    if (selection.empty()) {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::End();
        return;
    }

    if (selection.size() == 1) {
        Node* node = document_.scene().findNode(selection.primary());
        if (node == nullptr) {
            ImGui::TextUnformatted("Nothing selected");
            ImGui::End();
            return;
        }

        drawNodeHeader(*node);
        drawTransform(*node);
        drawMesh(*node);
        drawMaterial(*node);
        drawCamera(*node);
        drawLight(*node);
        drawAddComponent(*node);
    } else {
        // 多选视图只展示共有字段；编辑同步写入所有选中节点。
        drawMultiHeader(selection);
        drawMultiActive(selection);
        drawMultiTransform(selection);
    }

    if (!statusMessage_.empty())
        ImGui::TextUnformatted(statusMessage_.c_str());
    ImGui::End();
}

std::vector<Node*> InspectorPanel::collectNodes(const SelectionSet<NodeHandle>& selection) const {
    std::vector<Node*> nodes;
    for (const NodeHandle handle : selection.items())
        if (Node* node = document_.scene().findNode(handle))
            nodes.push_back(node);
    return nodes;
}

void InspectorPanel::drawNodeHeader(Node& node) {
    // InputText needs a writable buffer; a locally sized string doubles as one.
    std::string name{node.name()};
    name.resize(128, char(0));
    if (ImGui::InputText("Name", name.data(), name.size())) {
        node.setName(std::string{name.c_str()});
        document_.markDirty();
    }

    bool active = node.activeSelf();
    if (ImGui::Checkbox("Active", &active)) {
        node.setActive(active);
        document_.markDirty();
    }
    ImGui::Separator();
}

void InspectorPanel::drawAssetInspector() {
    const VirtualPath& path = *assetSelection_;
    if (!FILE_SYSTEM.isFile(path)) {
        // Deleted or renamed since it was picked (Project window operations or
        // external changes); the next selection change re-points the view.
        ImGui::TextUnformatted("Asset not found");
        return;
    }
    ImGui::TextDisabled("%s", path.relativePath().c_str());
    ImGui::Separator();

    switch (inferAssetType(path)) {
    case AssetType::Material: {
        const MaterialHandle handle = MATERIAL_MANAGER.load(path);
        const Material* material = MATERIAL_MANAGER.find(handle);
        if (material == nullptr || !(material->assetPath() == path)) {
            // load() falls back to the Error Material, which must never be
            // offered for editing under another asset's name.
            ImGui::TextUnformatted("Failed to load material");
            return;
        }
        materialInspector_.draw(handle);
        return;
    }
    case AssetType::Shader: ImGui::TextUnformatted("Type: Shader"); break;
    case AssetType::Mesh: ImGui::TextUnformatted("Type: Mesh"); break;
    case AssetType::Scene: ImGui::TextUnformatted("Type: Scene"); break;
    case AssetType::Texture: ImGui::TextUnformatted("Type: Texture"); break;
    case AssetType::Generic: ImGui::TextUnformatted("Type: Generic"); break;
    case AssetType::Unknown:
    default: ImGui::TextUnformatted("Type: Unknown"); break;
    }
    const auto guid = ASSET_DATABASE.findGuid(path);
    ImGui::Text("GUID: %s", guid ? guid->toString().c_str() : "-");
    ImGui::TextDisabled("No editor for this asset type yet");
}

} // namespace engine::editor
