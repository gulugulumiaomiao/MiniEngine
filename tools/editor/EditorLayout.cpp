#include "tools/editor/EditorLayout.h"
#include "imgui_internal.h"
#include <initializer_list>

namespace engine::editor {
namespace {
ImGuiDockNode* findCentralNode(ImGuiDockNode* node) {
    if (!node || node->IsCentralNode())
        return node;
    if (ImGuiDockNode* center = findCentralNode(node->ChildNodes[0]))
        return center;
    return findCentralNode(node->ChildNodes[1]);
}
} // namespace

void applyDefaultEditorLayout(ImGuiID dockId) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dockId;
    ImGuiID bottom, right, left;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24F, &bottom, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20F, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20F, &left, &center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Statistics", bottom);
    ImGui::DockBuilderDockWindow("Scene View", center);
    ImGui::DockBuilderFinish(dockId);
}

bool hasDockedPanelLayout() {
    for (const char* name : {"Hierarchy", "Inspector", "Project", "Scene View", "Statistics"}) {
        const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(name));
        if (settings && settings->DockId != 0)
            return true;
    }
    return false;
}

bool migrateLegacyEditorLayout(ImGuiID dockId) {
    const auto* stats = ImGui::FindWindowSettingsByID(ImHashStr("Statistics"));
    const auto* scene = ImGui::FindWindowSettingsByID(ImHashStr("Scene View"));
    if (stats || !scene || scene->DockId == 0)
        return false;
    ImGuiDockNode* center = ImGui::DockBuilderGetCentralNode(dockId);
    // Restored ini nodes have their central flag before DockSpace has populated
    // the root's cached CentralNode pointer on the first frame.
    if (!center)
        center = findCentralNode(ImGui::DockBuilderGetNode(dockId));
    if (!center)
        return false;
    const ImGuiID oldSceneDock = scene->DockId;
    const ImGuiID centerDock = center->ID;
    ImGui::DockBuilderDockWindow("Statistics", oldSceneDock);
    ImGui::DockBuilderDockWindow("Scene View", centerDock);
    ImGui::DockBuilderFinish(dockId);
    return true;
}

} // namespace engine::editor
