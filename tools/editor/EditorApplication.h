#pragma once

#include "runtime/application/Application.h"
#include "core/filesystem/VirtualPath.h"
#include "tools/editor/EditorConfig.h"
#include "tools/editor/HierarchyPanel.h"
#include "tools/editor/ImGuiLayer.h"
#include "tools/editor/InspectorPanel.h"
#include "tools/editor/ProjectPanel.h"
#include "tools/editor/ProjectPickerPanel.h"
#include "tools/editor/SceneDocument.h"
#include "tools/editor/SceneViewPanel.h"

#include <filesystem>
#include <optional>
#include <string>

namespace engine::editor {

// Editor entry point: owns the ImGui layer, the scene document and the panels, and
// drives them from the engine's application callbacks. The panels overlay the swapchain
// output, which acts as the scene view.
class EditorApplication final : public Application {
public:
    // editorConfigPath is the physical path to editor.json (next to the executable).
    // The editor config, including the recent-project registry, is loaded and saved at
    // this path; there is no virtual mount for editor configuration.
    explicit EditorApplication(const std::filesystem::path& editorConfigPath);
    ~EditorApplication() override;

    EditorApplication(const EditorApplication&) = delete;
    EditorApplication& operator=(const EditorApplication&) = delete;

private:
    // Application
    void onStart() override;
    void onUpdate(float deltaTime) override;
    void onStop() override;

    void openProject(const std::filesystem::path& root);
    void closeProject();
    void openScene(const VirtualPath& path);
    // Claims AssetManager's single change-listener slot for SceneDocument. Must be
    // re-run after every successful openProject: bringing the project subsystems up
    // tears them down first, and both steps touch that slot.
    void installSceneChangeListener();
    [[nodiscard]] bool saveDocument();
    void openSaveAsPopup();
    void handleShortcuts();
    void drawMenuBar();
    void drawDockSpace();
    void resetDockLayout();
    void drawConflictModal();
    void drawSaveAsPopup();

    ImGuiLayer imguiLayer_;
    SceneDocument document_;
    ProjectPickerPanel projectPicker_;
    ProjectPanel projectPanel_;
    HierarchyPanel hierarchyPanel_;
    InspectorPanel inspectorPanel_;
    SceneViewPanel sceneViewPanel_;
    bool saveAsOpen_{};
    char saveAsPath_[256]{};
    std::string statusMessage_;
    // Docking state: a default Unity-style layout is applied on the first frame that
    // shows the dock space unless the new ImGui context already restored the panels from
    // a persisted layout, and re-applied on demand via Window > Reset Layout. The gate
    // is reset per context (project open/create rebuilds the ImGui context). Panel
    // visibility is a session-level preference (windows keep their dock slots through
    // the persisted layout, so toggling is safe).
    bool dockLayoutApplied_{};
    bool forceApplyDefaultLayout_{};
    bool showProject_{true};
    bool showHierarchy_{true};
    bool showInspector_{true};
    bool showScene_{true};
    // A project to open, deferred out of the ImGui frame: picker clicks happen inside
    // an active ImGui window, and the migration tears the ImGui context down and
    // rebuilds it. Running it there would leave every later ImGui call with a freed
    // context (access violation in e.g. SameLine). onUpdate consumes it before
    // beginFrame, i.e. with no ImGui frame open.
    std::optional<std::filesystem::path> pendingProjectRoot_;
    bool pendingProjectClose_{};
};

} // namespace engine::editor
