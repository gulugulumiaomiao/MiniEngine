#pragma once

#include "asset/base/Asset.h"
#include "core/filesystem/VirtualPath.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace engine::editor {

struct ProjectEntry {
    VirtualPath path;
    std::string name;
    bool directory{};
};

// Unity-style Project browser model: navigation history, search/type filtering,
// single selection and the asset/file operations (create/rename/move/remove/
// duplicate/reimport). ProjectPanel is a thin view over this state; every side
// effect lives here so tests drive the browser without ImGui.
//
// Selection is single-entry by design (no Ctrl/Shift multi-select); context
// menus, drag-drop and deletion all act on the one selected entry.
class ProjectBrowserModel {
public:
    enum class ViewMode { Grid, List };

    // --- Navigation (history capped at 64 entries, oldest dropped) ---
    // Re-navigating the current directory does not push a history entry;
    // invalid directories are ignored.
    void navigate(const VirtualPath& directory);
    bool back();
    bool forward();
    bool navigateUp();
    [[nodiscard]] const VirtualPath& currentDirectory() const; // default assets://
    [[nodiscard]] bool canBack() const;
    [[nodiscard]] bool canForward() const;
    [[nodiscard]] std::vector<VirtualPath> breadcrumbs() const; // assets:// downwards

    // --- Filtering ---
    // A non-empty search text switches contentEntries() to a recursive match
    // over assets:// (files and directories, case-insensitive substring).
    void setSearchText(std::string text);
    [[nodiscard]] const std::string& searchText() const;
    // AssetType::Unknown doubles as "All": no type restriction.
    void setTypeFilter(AssetType type);
    [[nodiscard]] AssetType typeFilter() const;
    void setViewMode(ViewMode mode);
    [[nodiscard]] ViewMode viewMode() const;

    // --- Selection (single) ---
    // A selection whose path disappeared after invalidate()/operations is
    // cleared when snapshots rebuild.
    void selectEntry(const VirtualPath& path);
    void clearSelection();
    [[nodiscard]] const VirtualPath* selectedEntry() const; // nullptr when empty

    // --- Snapshots (cached; .meta hidden; directories first, then name) ---
    // All directories under assets://, flattened; the view builds the tree.
    [[nodiscard]] std::vector<ProjectEntry> directoryTreeEntries() const;
    // Current-directory children, or the recursive search result.
    [[nodiscard]] std::vector<ProjectEntry> contentEntries() const;
    void invalidate();

    // --- Operations (auto-invalidate on success; failures fill lastError) ---
    [[nodiscard]] const std::string& lastError() const;
    bool createFolder(const VirtualPath& parent, const std::string& name);
    bool createAsset(const VirtualPath& parent, AssetType type, const std::string& name);
    bool renameEntry(const VirtualPath& path, const std::string& newName);
    bool moveEntry(const VirtualPath& source, const VirtualPath& targetDir);
    bool removeEntry(const VirtualPath& path);
    bool duplicateEntry(const VirtualPath& path);
    bool reimportEntry(const VirtualPath& path);
    [[nodiscard]] std::vector<VirtualPath> dependentsOf(const VirtualPath& path) const;

private:
    void rebuildSnapshots() const;
    // Shared rename/move core: files go through the pipeline (GUID-preserving),
    // directories physically move first and then re-point their records.
    bool movePath(const VirtualPath& from, const VirtualPath& to);
    void fail(std::string error);
    [[nodiscard]] bool isSearchActive() const { return !searchText_.empty(); }

    VirtualPath current_{VirtualPath{"assets://"}};
    std::vector<VirtualPath> history_;
    std::size_t historyIndex_{};
    std::string searchText_;
    AssetType typeFilter_{AssetType::Unknown};
    ViewMode viewMode_{ViewMode::Grid};
    // mutable: dropping a stale selection is part of snapshot rebuilding, which
    // happens lazily inside the const accessors.
    mutable std::optional<VirtualPath> selected_;

    mutable std::vector<ProjectEntry> directoryTree_;
    mutable std::vector<ProjectEntry> content_;
    mutable bool snapshotsDirty_{true};
    // listFiles 只能看到文件，空目录对快照不可见；createFolder/movePath/duplicate
    // 把新建目录记在这里，让它们立即可见、可重命名（会话级，随 rebuild 清理失效项）。
    mutable std::set<std::string> knownDirectories_;
    std::string lastError_;
};

} // namespace engine::editor
