#pragma once

#include "core/filesystem/VirtualPath.h"
#include "tools/editor/ProjectBrowserModel.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct ImVec2;

namespace engine::editor {

// Unity-style Project window: a folder tree on the left, the current folder's
// entries (grid or list) on the right, breadcrumb/search/type-filter toolbar,
// single selection, and context menus for create/rename/move/delete/duplicate/
// reimport. All state and side effects live in ProjectBrowserModel; this class
// is the ImGui view.
class ProjectPanel {
public:
    using OpenSceneHandler = std::function<void(const VirtualPath&)>;

    explicit ProjectPanel(OpenSceneHandler openScene) : openScene_(std::move(openScene)) {}

    void draw();

    // Tests and the editor access the browser state through the model.
    [[nodiscard]] ProjectBrowserModel& model() { return model_; }

private:
    // Payload for ImGui drag-drop: a self-contained VirtualPath copy (ImGui
    // shallow-copies payloads, so pointer members would dangle).
    struct EntryPayload {
        char path[160]{};
        bool directory{};
    };

    void drawToolbar();
    void drawBreadcrumb(const VirtualPath& directory);
    void drawDirectoryTree();
    void drawTreeNodes(const VirtualPath& parent);
    void drawContent();
    void drawEntry(const ProjectEntry& entry, const ImVec2& cellSize);
    void drawEntryIconLabel(const ProjectEntry& entry, const ImVec2& cellMin,
                            const ImVec2& cellSize);
    void drawRenameInput(float width);
    void drawEntryContextMenu(const ProjectEntry& entry);
    void drawCreateMenu();
    void drawDropTarget(const VirtualPath& directory);
    void drawDeleteModal();
    void drawStatusLine();
    void handleShortcuts();
    void beginRename(const VirtualPath& path);
    void openInExplorer(const VirtualPath& path);
    void copyToClipboard(const std::string& text);
    void requestDelete(const VirtualPath& path);

    OpenSceneHandler openScene_;
    ProjectBrowserModel model_;

    // Filter toolbar state
    int typeFilterIndex_{};
    char searchText_[128]{};
    bool focusSearch_{};

    // In-place rename (HierarchyPanel-style)
    VirtualPath renameTarget_;
    std::vector<char> renameBuffer_;
    bool focusRename_{};

    // Delete confirmation modal
    std::optional<VirtualPath> deleteTarget_;
    std::size_t deleteDependentCount_{};
    bool deleteModalOpen_{};
    bool deleteModalPending_{};

    // Status feedback (failure mirror; cleared before each operation)
    std::string statusMessage_;
};

} // namespace engine::editor
