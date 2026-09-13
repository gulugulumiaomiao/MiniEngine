#include "tools/editor/HierarchyPanel.h"

#include "imgui.h"
#include "scene/scene/Scene.h"
#include "scene/node/Node.h"
#include "tools/editor/SceneDocument.h"

#include <cstring>
#include <string>

namespace engine::editor {

void HierarchyPanel::draw() {
    if (!ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    Scene& scene = document_.scene();
    const NodeHandle root = scene.rootHandle();
    const bool rootSelected = selection_ == root;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.72F, 0.78F, 0.86F, 1.0F});
    const bool rootOpen = ImGui::TreeNodeEx("Scene Root",
                                            ImGuiTreeNodeFlags_OpenOnArrow |
                                                ImGuiTreeNodeFlags_DefaultOpen |
                                                (rootSelected ? ImGuiTreeNodeFlags_Selected : 0));
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selection_ = root;
    if (rootOpen) {
        for (NodeHandle child : scene.root().children())
            drawNode(child, root);
        ImGui::TreePop();
    }

    ImGui::Separator();
    if (ImGui::Button("Create Node")) {
        const NodeHandle created = scene.createNode("Node");
        if (created)
            selection_ = created;
        document_.markDirty();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!selection_ || selection_ == root);
    if (ImGui::Button("Delete") && scene.destroyNode(selection_))
        selection_ = {};
    ImGui::EndDisabled();

    ImGui::End();
}

void HierarchyPanel::drawNode(NodeHandle handle, NodeHandle) {
    Scene& scene = document_.scene();
    Node* node = scene.findNode(handle);
    if (!node)
        return;

    ImGui::PushID(static_cast<int>(handle.index));

    const bool selected = selection_ == handle;
    const ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                         ImGuiTreeNodeFlags_SpanAvailWidth |
                                         (selected ? ImGuiTreeNodeFlags_Selected : 0);
    const ImGuiTreeNodeFlags flags =
        node->children().empty() ? baseFlags | ImGuiTreeNodeFlags_Leaf : baseFlags;

    const std::string label = node->activeSelf() ? std::string{node->name()}
                                                 : std::string{node->name()} + " (inactive)";
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selection_ = handle;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            renameTarget_ = handle;
            const std::string_view name = node->name();
            std::memcpy(renameBuffer_, name.data(), name.size());
            renameBuffer_[name.size()] = '\0';
        }
    }
    if (opened) {
        for (NodeHandle child : node->children())
            drawNode(child, handle);
        ImGui::TreePop();
    }

    if (renameTarget_ == handle) {
        ImGui::SetNextItemWidth(-1.0F);
        const bool committed = ImGui::InputText("##rename",
                                                renameBuffer_,
                                                sizeof(renameBuffer_),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        if (committed || ImGui::IsItemDeactivated()) {
            if (renameBuffer_[0] != '\0' && std::string{renameBuffer_} != node->name()) {
                node->setName(renameBuffer_);
                document_.markDirty();
            }
            renameTarget_ = {};
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            renameTarget_ = {};
    }

    ImGui::PopID();
}

} // namespace engine::editor
