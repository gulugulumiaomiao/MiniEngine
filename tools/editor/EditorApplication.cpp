#include "tools/editor/EditorApplication.h"

#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "runtime/engine/Engine.h"
#include "runtime/config/ProjectConfig.h"
#include "runtime/window/Window.h"

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder* for the default Unity layout

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace engine::editor {
namespace {

// Unity-style default layout: scene graph on the left, inspector on the right, project
// browser strip along the bottom, and the engine-rendered scene in between as the empty
// passthru central node (the swapchain output shows through the dock host).
void applyUnityLayout(ImGuiID dockId) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockId;
    ImGuiID bottom;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24F, &bottom, &center);
    ImGuiID right;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20F, &right, &center);
    ImGuiID left;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20F, &left, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Scene View", bottom);
    ImGui::DockBuilderFinish(dockId);
}

// True when the current ImGui context restored at least one of the editor panels from
// a persisted layout (imgui.ini). Fresh contexts have no such settings: the first run
// of the editor, and the otherwise-empty ini written while migrating the context during
// project open/create. In both cases the layout should fall back to the Unity default.
// A file-existence test alone would not work: migrating the context during project
// create writes an (empty) ini that would wrongly suppress the default layout.
bool hasDockedPanelLayout() {
    for (const char* name : {"Hierarchy", "Inspector", "Project", "Scene View"}) {
        const ImGuiWindowSettings* settings =
            ImGui::FindWindowSettingsByID(ImHashStr(name, 0, 0));
        if (settings != nullptr && settings->DockId != 0)
            return true;
    }
    return false;
}

} // namespace

EditorApplication::EditorApplication()
    : projectPicker_(ENGINE.editorConfig().registry),
      projectPanel_([this](const VirtualPath& path) { openScene(path); }),
      hierarchyPanel_(document_),
      inspectorPanel_(document_),
      sceneViewPanel_(document_) {
    // editor.json 固定在当前工作目录下的 editor/config/ 解析（不再由 main 传入）。
    const std::filesystem::path editorConfigPath =
        std::filesystem::current_path() / "editor" / "config" / "editor.json";
    // Load the editor config into the engine. The picker binds to the engine-owned
    // registry, so loading replaces the contents of the same object the picker
    // references; there is no application-side copy of the config.
    ENGINE.loadEditorConfig(editorConfigPath);

    // The ImGui layout (imgui.ini) lives next to the editor config and holds the
    // docking layout between sessions.
    imguiLayer_.setIniPath((editorConfigPath.parent_path() / "imgui.ini").string());

    projectPicker_.setOpenHandler([this](const std::filesystem::path& root) {
        pendingProjectRoot_ = root;
    });
    projectPicker_.setCreateHandler(
        [this](const std::filesystem::path& parent, const std::string& name) {
            pendingProjectRoot_ = parent / name;
        });
    projectPicker_.setChangedHandler([]() { ENGINE.saveEditorConfig(); });
}

EditorApplication::~EditorApplication() {
    imguiLayer_.detach();
}

void EditorApplication::onStart() {
    imguiLayer_.attach(ENGINE.renderer(), ENGINE.window());

    installSceneChangeListener();

    if (!ENGINE.isProjectOpen()) {
        projectPicker_.show();
    }
}

void EditorApplication::onUpdate(float deltaTime) {
    (void)deltaTime;
    // A picker click deferred its work here so the ImGui layer (and the renderer it
    // binds to) can be torn down and rebuilt with no ImGui frame open.
    if (pendingProjectClose_) {
        pendingProjectClose_ = false;
        closeProject();
    }
    if (pendingProjectRoot_) {
        const std::filesystem::path root = *pendingProjectRoot_;
        pendingProjectRoot_.reset();
        openProject(root);
    }

    imguiLayer_.beginFrame();
    hierarchyPanel_.syncDocument();

    if (projectPicker_.draw()) {
        imguiLayer_.endFrame();
        return;
    }

    if (!ENGINE.isProjectOpen()) {
        ImGui::TextDisabled("No project open. Use File > Open Project to select one.");
        imguiLayer_.endFrame();
        return;
    }

    if (!document_.valid())
        document_.createEmpty();

    handleShortcuts();
    drawMenuBar();
    drawDockSpace();
    drawConflictModal();
    drawSaveAsPopup();
    if (showProject_)
        projectPanel_.draw();
    hierarchyPanel_.syncDocument();
    if (showHierarchy_)
        hierarchyPanel_.draw();
    if (showInspector_)
        inspectorPanel_.draw(hierarchyPanel_.selectionSet());
    if (showScene_)
        sceneViewPanel_.draw();

    imguiLayer_.endFrame();
}

void EditorApplication::onStop() {
    // 先释放依赖 Renderer 的界面资源；Engine::shutdown 统一保存配置。
    imguiLayer_.detach();
}

void EditorApplication::installSceneChangeListener() {
    // AssetManager has a single listener slot, so this replaces whatever was installed
    // before: the Engine's own auto-reload listener, or nothing at all once
    // ASSET_MANAGER.shutdown() has run. The editor needs the slot for itself because
    // SceneDocument owns the reload policy -- clean documents reload silently, dirty
    // ones raise the conflict modal instead of losing local edits.
    ASSET_MANAGER.setChangeListener(
        [this](const VirtualPath& path, AssetType type, bool removed) {
            if (type == AssetType::Scene)
                document_.handleExternalChange(path, removed);
        });
}

void EditorApplication::openProject(const std::filesystem::path& root) {
    // OpenProject destroys the current renderer (and window on size change) and builds
    // a new one. The ImGui layer is bound to the old renderer/device, so move it off
    // before the renderer goes away and back on once the new one exists. Otherwise the
    // new renderer has no overlay: frames it presents with an empty draw list leave the
    // swapchain backbuffer in an undefined layout.
    imguiLayer_.detach();

    if (!ENGINE.openProject(root)) {
        document_.createEmpty();
        hierarchyPanel_.select({});
        imguiLayer_.attach(ENGINE.renderer(), ENGINE.window());
        installSceneChangeListener();
        statusMessage_ = "Failed to open project: " + root.string();
        Log::error("EditorApplication", "Failed to open project: %s", root.string().c_str());
        projectPicker_.show();
        return;
    }

    // openProject() tore the project subsystems down (ASSET_MANAGER.shutdown() clears
    // the listener) and then brought them back up, which installs the Engine's own
    // auto-reload listener. Take the slot back now, otherwise every external scene
    // change bypasses SceneDocument: the Engine would reload behind our back, rebuild
    // the Scene and invalidate the NodeHandle held by HierarchyPanel, and
    // suppressNextChange_ would never get to swallow the editor's own atomic write.
    installSceneChangeListener();

    imguiLayer_.attach(ENGINE.renderer(), ENGINE.window());

    // The ImGui context was rebuilt above, so the docking state must be re-derived from
    // what the new context actually restored: apply the default Unity layout unless the
    // persisted layout (imgui.ini) brought the panels back already docked.
    dockLayoutApplied_ = false;
    forceApplyDefaultLayout_ = false;

    ENGINE.editorConfig().registry.addProject(root, ENGINE.projectConfig().name);
    if (ENGINE.activeScenePath().valid())
        document_.open(ENGINE.activeScenePath());
    else
        document_.createEmpty();
    hierarchyPanel_.select({});
    ENGINE.saveEditorConfig();
    statusMessage_.clear();
}

void EditorApplication::closeProject() {
    // 在帧外切换 Renderer，返回选择界面后仍能继续绘制和打开项目。
    imguiLayer_.detach();
    ENGINE.closeProject();
    document_.createEmpty();
    hierarchyPanel_.select({});
    imguiLayer_.attach(ENGINE.renderer(), ENGINE.window());
    installSceneChangeListener();
    dockLayoutApplied_ = false;
    forceApplyDefaultLayout_ = false;
    statusMessage_.clear();
    projectPicker_.show();
}

void EditorApplication::openScene(const VirtualPath& path) {
    if (document_.open(path)) {
        hierarchyPanel_.select({});
    }
}

bool EditorApplication::saveDocument() {
    std::string error;
    if (document_.save(error)) {
        statusMessage_.clear();
        return true;
    }
    statusMessage_ = "Save failed: " + error;
    Log::error("EditorApplication", "Save failed: %s", error.c_str());
    return false;
}

void EditorApplication::openSaveAsPopup() {
    std::string initial = "assets://scenes/scene.scene.json";
    if (!document_.untitled())
        initial = document_.sourcePath().string();
    std::memset(saveAsPath_, 0, sizeof(saveAsPath_));
    std::memcpy(saveAsPath_, initial.c_str(),
                std::min(initial.size(), sizeof(saveAsPath_) - 1));
    saveAsOpen_ = true;
}

void EditorApplication::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (document_.untitled())
            openSaveAsPopup();
        else
            (void)saveDocument();
    }
}

void EditorApplication::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene")) {
            document_.createEmpty();
            hierarchyPanel_.select({});
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, document_.valid() && !document_.untitled()))
            (void)saveDocument();
        if (ImGui::MenuItem("Save As...", nullptr, false, document_.valid()))
            openSaveAsPopup();
        ImGui::Separator();
        if (ImGui::MenuItem("Open Project..."))
            projectPicker_.show();
        if (ImGui::MenuItem("Close Project", nullptr, false, ENGINE.isProjectOpen()))
            pendingProjectClose_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            ENGINE.requestQuit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        ImGui::MenuItem("Project", nullptr, &showProject_);
        ImGui::MenuItem("Scene View", nullptr, &showScene_);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            resetDockLayout();
        ImGui::EndMenu();
    }

    const char* marker = document_.dirty() ? " *" : "";
    ImGui::Separator();
    if (ENGINE.isProjectOpen())
        ImGui::Text("[%s]", ENGINE.projectConfig().name.c_str());
    ImGui::Separator();
    ImGui::Text("%s%s", document_.displayName().c_str(), marker);
    if (!statusMessage_.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.9F, 0.4F, 0.4F, 1.0F});
        ImGui::TextWrapped("%s", statusMessage_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndMainMenuBar();
}

void EditorApplication::drawDockSpace() {
    const ImGuiID dockId = ImGui::GetID("MiniEditorDockSpace");
    if (!dockLayoutApplied_) {
        dockLayoutApplied_ = true;
        // The default Unity layout is applied when no persisted layout was loaded for
        // the panels (first run, or an empty ini written during context migration),
        // or when the user explicitly asks to reset it.
        if (forceApplyDefaultLayout_ || !hasDockedPanelLayout()) {
            forceApplyDefaultLayout_ = false;
            applyUnityLayout(dockId);
        }
    }

    // A full-viewport, non-dockable host window that owns the dockspace. The central
    // node is left as a passthru hole so the engine-rendered frame shows through as the
    // scene view; everything else is covered by the docked panels.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
    if (ImGui::Begin("##MiniEditorDockHost", nullptr,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground)) {
        ImGui::DockSpace(dockId, ImVec2(0.0F, 0.0F), ImGuiDockNodeFlags_PassthruCentralNode);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void EditorApplication::resetDockLayout() {
    dockLayoutApplied_ = false;
    forceApplyDefaultLayout_ = true;
    showProject_ = showHierarchy_ = showInspector_ = showScene_ = true;
}

void EditorApplication::drawConflictModal() {
    if (!document_.conflictPending())
        return;
    if (!ImGui::IsPopupOpen("ExternalChangePopup"))
        ImGui::OpenPopup("ExternalChangePopup");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    if (!ImGui::BeginPopupModal("ExternalChangePopup", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("The scene file changed on disk while this document has "
                           "unsaved edits.");
    ImGui::Text("File: %s", document_.sourcePath().string().c_str());
    ImGui::Separator();
    if (ImGui::Button("Reload From Disk (discard local edits)")) {
        document_.resolveConflict(SceneDocument::ConflictPolicy::ReloadFromDisk);
        hierarchyPanel_.select({});
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep Local (overwrite on save)")) {
        document_.resolveConflict(SceneDocument::ConflictPolicy::KeepLocal);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApplication::drawSaveAsPopup() {
    if (!saveAsOpen_)
        return;
    if (!ImGui::IsPopupOpen("SaveAsPopup"))
        ImGui::OpenPopup("SaveAsPopup");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    if (!ImGui::BeginPopupModal("Save As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Scene path (assets scheme, .scene.json):");
    ImGui::InputText("##path", saveAsPath_, sizeof(saveAsPath_));
    if (ImGui::Button("Save")) {
        const VirtualPath target{saveAsPath_};
        std::string error;
        if (document_.saveAs(target, error)) {
            saveAsOpen_ = false;
            statusMessage_.clear();
        } else {
            statusMessage_ = "Save As failed: " + error;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        saveAsOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace engine::editor
