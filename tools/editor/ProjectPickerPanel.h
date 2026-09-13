#pragma once

#if defined(MINI_EDITOR)

#include "tools/editor/ProjectRegistry.h"

#include <filesystem>
#include <functional>
#include <string>

namespace engine::editor {

// Modal dialog shown when the editor starts without a project or when the user
// explicitly requests a project change. Lists recent projects from the registry and
// offers new / browse / remove / delete actions.
class ProjectPickerPanel {
public:
    using OpenHandler = std::function<void(const std::filesystem::path&)>;
    using CreateHandler = std::function<void(const std::filesystem::path& parent,
                                             const std::string& name)>;

    explicit ProjectPickerPanel(ProjectRegistry& registry);

    void setOpenHandler(OpenHandler handler) { openHandler_ = std::move(handler); }
    void setCreateHandler(CreateHandler handler) { createHandler_ = std::move(handler); }
    // Invoked whenever registry state changes (project removed/deleted) so the owner can
    // persist editor.json. The registry is embedded in the editor config and is written
    // through EditorConfig::save, never through ProjectRegistry::save() (which would
    // drop the window preference and schema fields of editor.json).
    void setChangedHandler(std::function<void()> handler) { changedHandler_ = std::move(handler); }

    // Draws the modal. Returns true while the picker is still visible.
    [[nodiscard]] bool draw();

    void show() { visible_ = true; }
    void hide() { visible_ = false; }
    [[nodiscard]] bool visible() const { return visible_; }

private:
    void drawProjectList();
    void drawNewProjectForm();
    void openFolderPicker();
    void notifyChanged();

    ProjectRegistry& registry_;
    OpenHandler openHandler_;
    CreateHandler createHandler_;
    std::function<void()> changedHandler_;
    bool visible_{};
    bool showNewProjectForm_{};
    char newProjectName_[128]{};
    std::filesystem::path newProjectParent_;
    std::string statusMessage_;
};

// Opens a native Windows folder picker and returns the selected path.
[[nodiscard]] std::filesystem::path openNativeFolderPicker();

} // namespace engine::editor

#endif // MINI_EDITOR
