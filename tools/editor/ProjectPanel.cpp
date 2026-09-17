#include "tools/editor/ProjectPanel.h"

#include "asset/base/Asset.h"
#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "core/filesystem/FileSystem.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// ShellExecuteW lives in shellapi.h, which WIN32_LEAN_AND_MEAN's Windows.h skips.
#include <shellapi.h>
#endif

namespace engine::editor {
namespace {

constexpr const char* kEntryPayload = "MINI_PROJECT_ENTRY";
constexpr float kMinTreeWidth = 150.0F;
constexpr float kTreeWidthRatio = 0.25F;
constexpr float kGridCellWidth = 96.0F;
constexpr float kGridCellHeight = 76.0F;
constexpr float kCellPadding = 4.0F;

struct TypeFilterOption {
    const char* label;
    AssetType type;
};
constexpr TypeFilterOption kTypeFilters[] = {
    {"All", AssetType::Unknown},       {"Scene", AssetType::Scene},
    {"Material", AssetType::Material}, {"Mesh", AssetType::Mesh},
    {"Shader", AssetType::Shader},     {"Texture", AssetType::Texture},
};

// Type icons: colored letters until thumbnail rendering exists.
[[nodiscard]] const char* iconFor(const ProjectEntry& entry) {
    if (entry.directory)
        return "D";
    switch (inferAssetType(entry.path)) {
    case AssetType::Scene: return "S";
    case AssetType::Material: return "M";
    case AssetType::Mesh: return "T";
    case AssetType::Shader: return "R";
    case AssetType::Texture: return "P";
    case AssetType::Generic: return "G";
    default: return "?";
    }
}

[[nodiscard]] const ImVec4& iconColorFor(const ProjectEntry& entry) {
    static const ImVec4 kDirectory{0.78F, 0.72F, 0.42F, 1.0F};
    static const ImVec4 kScene{0.42F, 0.68F, 0.92F, 1.0F};
    static const ImVec4 kMaterial{0.58F, 0.78F, 0.42F, 1.0F};
    static const ImVec4 kMesh{0.72F, 0.52F, 0.86F, 1.0F};
    static const ImVec4 kShader{0.92F, 0.58F, 0.38F, 1.0F};
    static const ImVec4 kTexture{0.48F, 0.80F, 0.74F, 1.0F};
    static const ImVec4 kOther{0.62F, 0.66F, 0.70F, 1.0F};
    if (entry.directory)
        return kDirectory;
    switch (inferAssetType(entry.path)) {
    case AssetType::Scene: return kScene;
    case AssetType::Material: return kMaterial;
    case AssetType::Mesh: return kMesh;
    case AssetType::Shader: return kShader;
    case AssetType::Texture: return kTexture;
    default: return kOther;
    }
}

// Grid labels are drawn straight into the draw list so they never steal hover
// or clicks from the Selectable underneath (real widgets would win the hover
// test and make cell clicks dead).
void drawEllipsized(ImDrawList* drawList,
                    const ImVec2& position,
                    float maxWidth,
                    ImU32 color,
                    const std::string& text) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        drawList->AddText(position, color, text.c_str());
        return;
    }
    std::string clipped{text};
    while (!clipped.empty() &&
           ImGui::CalcTextSize((clipped + "...").c_str()).x > maxWidth)
        clipped.pop_back();
    drawList->AddText(position, color, (clipped + "...").c_str());
}

// One line if it fits, otherwise break once (at a space or dot when possible)
// and ellipsize the remainder.
void drawGridLabel(ImDrawList* drawList,
                   const ImVec2& position,
                   float maxWidth,
                   ImU32 color,
                   const std::string& name) {
    if (ImGui::CalcTextSize(name.c_str()).x <= maxWidth) {
        drawList->AddText(position, color, name.c_str());
        return;
    }
    std::size_t cut = name.size();
    while (cut > 0 && ImGui::CalcTextSize(name.c_str(), name.c_str() + cut).x > maxWidth)
        --cut;
    const std::size_t breakAt = name.find_last_of(" .", cut);
    if (breakAt != std::string::npos && breakAt > 0)
        cut = breakAt;
    drawList->AddText(position, color, name.c_str(), name.c_str() + cut);
    drawEllipsized(drawList,
                   ImVec2{position.x, position.y + ImGui::GetTextLineHeight()},
                   maxWidth,
                   color,
                   name.substr(std::min(cut + 1, name.size())));
}

int resizeRenameBuffer(ImGuiInputTextCallbackData* data) {
    auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
    buffer.resize(static_cast<std::size_t>(data->BufSize));
    data->Buf = buffer.data();
    return 0;
}

// Directories without sub-folders render as leaves (no expander arrow) — the
// flattened directory tree only contains folders, so a parent match is enough.
[[nodiscard]] bool hasSubFolders(const std::vector<ProjectEntry>& tree,
                                 const VirtualPath& directory) {
    return std::ranges::any_of(tree, [&directory](const ProjectEntry& entry) {
        return entry.path.parent() == directory;
    });
}

} // namespace

void ProjectPanel::draw() {
    if (!ImGui::Begin("Project", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (!FILE_SYSTEM.isMounted("assets")) {
        ImGui::TextDisabled("No project open");
        ImGui::End();
        return;
    }

    drawToolbar();

    const ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float childHeight = contentSize.y - ImGui::GetTextLineHeightWithSpacing();
    const float treeWidth =
        std::max(contentSize.x * kTreeWidthRatio, kMinTreeWidth);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0F);
    if (ImGui::BeginChild("##tree", ImVec2{treeWidth, childHeight},
                          ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders)) {
        drawDirectoryTree();
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::BeginChild("##content", ImVec2{0.0F, childHeight}, ImGuiChildFlags_Borders)) {
        drawContent();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    handleShortcuts();
    drawStatusLine();
    drawDeleteModal();
    ImGui::End();
}

void ProjectPanel::drawToolbar() {
    ImGui::BeginDisabled(!model_.canBack());
    if (ImGui::ArrowButton("##back", ImGuiDir_Left))
        model_.back();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!model_.canForward());
    if (ImGui::ArrowButton("##forward", ImGuiDir_Right))
        model_.forward();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(model_.currentDirectory().string() == "assets://");
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        model_.navigateUp();
    ImGui::EndDisabled();
    ImGui::SameLine();
    for (const VirtualPath& crumb : model_.breadcrumbs()) {
        drawBreadcrumb(crumb);
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (focusSearch_) {
        ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
    }
    ImGui::SetNextItemWidth(180.0F);
    if (ImGui::InputTextWithHint("##search", "Search assets...", searchText_,
                                 sizeof(searchText_))) {
        model_.setSearchText(searchText_);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0F);
    if (ImGui::BeginCombo("##type", kTypeFilters[typeFilterIndex_].label)) {
        for (int index = 0; index < IM_ARRAYSIZE(kTypeFilters); ++index) {
            if (ImGui::Selectable(kTypeFilters[index].label, index == typeFilterIndex_)) {
                typeFilterIndex_ = index;
                model_.setTypeFilter(kTypeFilters[index].type);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Grid", model_.viewMode() == ProjectBrowserModel::ViewMode::Grid))
        model_.setViewMode(ProjectBrowserModel::ViewMode::Grid);
    ImGui::SameLine();
    if (ImGui::RadioButton("List", model_.viewMode() == ProjectBrowserModel::ViewMode::List))
        model_.setViewMode(ProjectBrowserModel::ViewMode::List);
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        model_.invalidate();
}

void ProjectPanel::drawBreadcrumb(const VirtualPath& directory) {
    const std::string label =
        directory.string() == "assets://" ? "Assets" : directory.filename();
    if (ImGui::SmallButton(label.c_str()))
        model_.navigate(directory);
    drawDropTarget(directory);
}

void ProjectPanel::drawDirectoryTree() {
    const bool isRoot = model_.currentDirectory().string() == "assets://";
    const std::vector<ProjectEntry> all = model_.directoryTreeEntries();
    // An empty project renders the root as a leaf (no expander arrow).
    const bool rootLeaf = !hasSubFolders(all, VirtualPath{"assets://"});
    ImGuiTreeNodeFlags rootFlags =
        rootLeaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                 : ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;
    if (isRoot)
        rootFlags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.78F, 0.82F, 0.88F, 1.0F});
    const bool rootOpen = ImGui::TreeNodeEx("Assets", rootFlags);
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        model_.navigate(VirtualPath{"assets://"});
    drawDropTarget(VirtualPath{"assets://"});
    // Leaves report open == true (TreeNodeUpdateNextOpen) but never pushed
    // (NoTreePushOnOpen), so the pop must be skipped to keep the stack paired.
    if (rootOpen && !rootLeaf) {
        drawTreeNodes(VirtualPath{"assets://"});
        ImGui::TreePop();
    }
}

void ProjectPanel::drawTreeNodes(const VirtualPath& parent) {
    // directoryTreeEntries is the cached flattened list; pick this parent's
    // direct children and recurse.
    const std::vector<ProjectEntry> all = model_.directoryTreeEntries();
    for (const ProjectEntry& entry : all) {
        if (entry.path.parent() != parent)
            continue;
        // Folders without sub-folders render as leaves: no expander arrow, and
        // a click anywhere on the row (arrow zone included) selects it.
        const bool leaf = !hasSubFolders(all, entry.path);
        ImGuiTreeNodeFlags flags =
            leaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                 : ImGuiTreeNodeFlags_OpenOnArrow;
        const bool selected = model_.selectedEntry() && *model_.selectedEntry() == entry.path;
        if (selected || model_.currentDirectory() == entry.path)
            flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushStyleColor(ImGuiCol_Text, iconColorFor(entry));
        const bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags);
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            model_.selectEntry(entry.path);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            model_.navigate(entry.path);
        drawDropTarget(entry.path);
        if (open && !leaf) { // Leaf returns true without pushing (see above).
            drawTreeNodes(entry.path);
            ImGui::TreePop();
        }
    }
}

void ProjectPanel::drawContent() {
    const std::vector<ProjectEntry> entries = model_.contentEntries();
    const bool grid = model_.viewMode() == ProjectBrowserModel::ViewMode::Grid;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const int columns =
        grid ? std::max(static_cast<int>(availableWidth / kGridCellWidth), 1) : 1;
    if (entries.empty())
        ImGui::TextDisabled(model_.searchText().empty() ? "Empty folder" : "No matches");

    int column = 0;
    for (const ProjectEntry& entry : entries) {
        ImGui::PushID(entry.path.string().c_str());
        if (grid && column > 0)
            ImGui::SameLine();
        drawEntry(entry,
                  grid ? ImVec2{kGridCellWidth, kGridCellHeight}
                       : ImVec2{availableWidth, ImGui::GetTextLineHeightWithSpacing()});
        if (grid && ++column == columns)
            column = 0;
        ImGui::PopID();
    }

    // Empty area below the entries: click clears the selection, right-click
    // offers Create. The window-level fallback covers the remaining blank spots
    // when the entries fill the whole area (NoOpenOverExistingPopup keeps entry
    // menus winning over this one).
    const ImVec2 remaining = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##empty-area",
                           ImVec2{std::max(remaining.x, 1.0F),
                                  std::max(remaining.y, ImGui::GetTextLineHeight())});
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        model_.clearSelection();
    if (ImGui::BeginPopupContextItem("##empty-menu"))
        drawCreateMenu();
    if (ImGui::BeginPopupContextWindow("##content-menu",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
        drawCreateMenu();
    }
}

void ProjectPanel::drawEntry(const ProjectEntry& entry, const ImVec2& cellSize) {
    const bool grid = model_.viewMode() == ProjectBrowserModel::ViewMode::Grid;
    const bool selected = model_.selectedEntry() && *model_.selectedEntry() == entry.path;

    const ImVec2 cellMin = ImGui::GetCursorScreenPos();
    // Grid cells need the explicit size so the whole cell is clickable; list
    // rows keep size {0,0} (SpanAvailWidth full row, label-height pitch).
    const bool clicked = ImGui::Selectable(
        "##entry",
        selected,
        grid ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_AllowDoubleClick,
        grid ? cellSize : ImVec2{});
    const ImVec2 after = ImGui::GetCursorScreenPos();
    const bool hovered = ImGui::IsItemHovered();
    const bool doubleClicked =
        hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    if (ImGui::BeginDragDropSource()) {
        EntryPayload payload{};
        std::strncpy(payload.path, entry.path.string().c_str(), sizeof(payload.path) - 1);
        payload.directory = entry.directory;
        ImGui::SetDragDropPayload(kEntryPayload, &payload, sizeof(payload));
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (entry.directory)
        drawDropTarget(entry.path);
    drawEntryContextMenu(entry);

    if (clicked)
        model_.selectEntry(entry.path); // Single selection by design.
    if (doubleClicked) {
        if (entry.directory) {
            model_.navigate(entry.path);
        } else if (inferAssetType(entry.path) == AssetType::Scene && openScene_) {
            openScene_(entry.path);
        }
    }

    drawEntryIconLabel(entry, cellMin, cellSize);
    if (renameTarget_ == entry.path) {
        ImGui::SetCursorScreenPos(cellMin);
        drawRenameInput(grid ? cellSize.x - 2.0F * kCellPadding
                             : cellSize.x - kCellPadding);
    }

    // Overlay widgets (the rename input) moved the cursor; restore the layout
    // position so SameLine()/wrapping stays consistent.
    ImGui::SetCursorScreenPos(after);
}

void ProjectPanel::drawEntryIconLabel(const ProjectEntry& entry,
                                      const ImVec2& cellMin,
                                      const ImVec2& cellSize) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 iconColor = ImGui::GetColorU32(iconColorFor(entry));
    const char* icon = iconFor(entry);
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    const bool grid = model_.viewMode() == ProjectBrowserModel::ViewMode::Grid;

    ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_Text);
    const auto record = entry.directory ? std::nullopt : ASSET_DATABASE.findByPath(entry.path);
    if (record && record->status == AssetImportStatus::Failed)
        labelColor = ImGui::GetColorU32(ImVec4{0.95F, 0.40F, 0.40F, 1.0F});

    if (grid) {
        const ImVec2 iconPos{cellMin.x + (cellSize.x - iconSize.x) * 0.5F, cellMin.y + kCellPadding};
        drawList->AddText(iconPos, iconColor, icon);
        const float labelWidth = cellSize.x - 2.0F * kCellPadding;
        const ImVec2 labelPos{cellMin.x + kCellPadding,
                              cellMin.y + kCellPadding + iconSize.y + kCellPadding};
        drawGridLabel(drawList, labelPos, labelWidth, labelColor, entry.name);
    } else {
        drawList->AddText(cellMin, iconColor, icon);
        const float labelOffset = iconSize.x + ImGui::GetStyle().ItemSpacing.x;
        const ImVec2 labelPos{cellMin.x + labelOffset, cellMin.y};
        // The list Selectable spans the full row width; clip the label to it.
        drawEllipsized(drawList,
                       labelPos,
                       ImGui::GetItemRectMax().x - labelPos.x - 4.0F,
                       labelColor,
                       entry.name);
    }

    // Import status tooltips ride on the selectable's hover (captured before
    // the overlays drew).
    if (ImGui::IsItemHovered()) {
        if (record && record->status == AssetImportStatus::Failed && !record->lastError.empty())
            ImGui::SetTooltip("%s", record->lastError.c_str());
        else if (!entry.directory && !record)
            ImGui::SetTooltip("Not imported");
    }
}

void ProjectPanel::drawRenameInput(float width) {
    if (focusRename_) {
        ImGui::SetKeyboardFocusHere();
        focusRename_ = false;
    }
    ImGui::SetNextItemWidth(width);
    const bool committed =
        ImGui::InputText("##rename",
                         renameBuffer_.data(),
                         renameBuffer_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize |
                             ImGuiInputTextFlags_AutoSelectAll,
                         resizeRenameBuffer,
                         &renameBuffer_);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        renameTarget_ = {};
    } else if (committed || ImGui::IsItemDeactivated()) {
        const VirtualPath target = renameTarget_;
        renameTarget_ = {};
        if (renameBuffer_[0] != '\0' && std::string{renameBuffer_.data()} != target.filename()) {
            statusMessage_.clear();
            if (!model_.renameEntry(target, renameBuffer_.data()))
                statusMessage_ = model_.lastError();
        }
    }
}

void ProjectPanel::drawEntryContextMenu(const ProjectEntry& entry) {
    if (!ImGui::BeginPopupContextItem("##entry-menu"))
        return;
    model_.selectEntry(entry.path);

    const bool directory = entry.directory;
    const AssetType type = inferAssetType(entry.path);

    if (!directory && ImGui::MenuItem("Open")) {
        if (type == AssetType::Scene && openScene_)
            openScene_(entry.path);
    }
    if (!directory && ImGui::MenuItem("Reimport")) {
        statusMessage_.clear();
        if (!model_.reimportEntry(entry.path))
            statusMessage_ = model_.lastError();
    }
    if (ImGui::MenuItem("Rename", "F2"))
        beginRename(entry.path);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        statusMessage_.clear();
        if (!model_.duplicateEntry(entry.path))
            statusMessage_ = model_.lastError();
    }
    if (ImGui::MenuItem("Copy Path"))
        copyToClipboard(entry.path.string());
    if (!directory) {
        const auto record = ASSET_DATABASE.findByPath(entry.path);
        if (record && ImGui::MenuItem("Copy GUID"))
            copyToClipboard(record->id.toString());
    }
    if (ImGui::MenuItem("Show in Explorer"))
        openInExplorer(entry.path);
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del"))
        requestDelete(entry.path);
    ImGui::EndPopup();
}

void ProjectPanel::drawCreateMenu() {
    if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Folder"))
            (void)model_.createFolder(model_.currentDirectory(), "New Folder");
        if (ImGui::MenuItem("Scene"))
            (void)model_.createAsset(model_.currentDirectory(), AssetType::Scene, "New Scene");
        if (ImGui::MenuItem("Material"))
            (void)model_.createAsset(model_.currentDirectory(), AssetType::Material,
                                     "New Material");
        if (ImGui::MenuItem("Shader"))
            (void)model_.createAsset(model_.currentDirectory(), AssetType::Shader, "New Shader");
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Refresh"))
        model_.invalidate();
    ImGui::EndPopup();
}

void ProjectPanel::drawDropTarget(const VirtualPath& directory) {
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kEntryPayload)) {
        const auto* entryPayload = static_cast<const EntryPayload*>(payload->Data);
        const VirtualPath source{std::string{entryPayload->path}};
        if (source.valid()) {
            statusMessage_.clear();
            if (!model_.moveEntry(source, directory))
                statusMessage_ = model_.lastError();
        }
    }
    ImGui::EndDragDropTarget();
}

void ProjectPanel::drawDeleteModal() {
    if (deleteModalPending_) {
        deleteModalPending_ = false;
        ImGui::OpenPopup("Confirm Delete");
    }
    if (!deleteModalOpen_ || !deleteTarget_)
        return;
    ImGui::SetNextWindowSize(ImVec2{380, 0}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Closed by clicking outside: cancel.
        deleteModalOpen_ = false;
        deleteTarget_.reset();
        return;
    }
    ImGui::Text("Delete \"%s\"?", deleteTarget_->filename().c_str());
    if (deleteDependentCount_ > 0)
        ImGui::TextColored(ImVec4{0.95F, 0.72F, 0.30F, 1.0F},
                           "Referenced by %d asset(s)",
                           static_cast<int>(deleteDependentCount_));
    ImGui::Separator();
    if (ImGui::Button("Delete", ImVec2{120, 0})) {
        statusMessage_.clear();
        if (!model_.removeEntry(*deleteTarget_))
            statusMessage_ = model_.lastError();
        deleteModalOpen_ = false;
        deleteTarget_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2{120, 0})) {
        deleteModalOpen_ = false;
        deleteTarget_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void ProjectPanel::drawStatusLine() {
    ImGui::Separator();
    if (!statusMessage_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.95F, 0.40F, 0.40F, 1.0F});
        ImGui::TextUnformatted(statusMessage_.c_str());
        ImGui::PopStyleColor();
        return;
    }
    const VirtualPath* selected = model_.selectedEntry();
    if (!selected) {
        ImGui::TextDisabled("%d items", static_cast<int>(model_.contentEntries().size()));
        return;
    }
    std::string summary = selected->filename();
    if (const auto record = ASSET_DATABASE.findByPath(*selected)) {
        summary += "  |  ";
        summary += assetTypeName(record->type);
        summary += "  |  ";
        summary += record->id.toString().substr(0, 13);
        summary += "...";
        switch (record->status) {
        case AssetImportStatus::Imported: summary += "  |  Imported"; break;
        case AssetImportStatus::Failed: summary += "  |  Failed"; break;
        case AssetImportStatus::Missing: summary += "  |  Missing"; break;
        case AssetImportStatus::NotImported: summary += "  |  Not imported"; break;
        }
    }
    ImGui::TextDisabled("%s", summary.c_str());
    if (const auto record = ASSET_DATABASE.findByPath(*selected);
        record && record->status == AssetImportStatus::Failed && !record->lastError.empty() &&
        ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", record->lastError.c_str());
    }
}

void ProjectPanel::handleShortcuts() {
    if (renameTarget_.valid() || deleteModalOpen_)
        return;
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F2) && model_.selectedEntry())
        beginRename(*model_.selectedEntry());
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete) && model_.selectedEntry())
        requestDelete(*model_.selectedEntry());
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && model_.selectedEntry()) {
        statusMessage_.clear();
        if (!model_.duplicateEntry(*model_.selectedEntry()))
            statusMessage_ = model_.lastError();
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F))
        focusSearch_ = true;
}

void ProjectPanel::beginRename(const VirtualPath& path) {
    renameTarget_ = path;
    // Files prefill with the stem (the recognized suffix survives the rename
    // in the model); directories use the full name.
    std::string name = path.filename();
    if (FILE_SYSTEM.isFile(path)) {
        for (const std::string_view suffix :
             {".scene.json", ".material.json", ".mesh.json", ".shader.json"}) {
            if (name.ends_with(suffix) && name.size() > suffix.size()) {
                name.resize(name.size() - suffix.size());
                break;
            }
        }
    }
    renameBuffer_.assign(name.begin(), name.end());
    renameBuffer_.push_back('\0');
    focusRename_ = true;
}

void ProjectPanel::openInExplorer(const VirtualPath& path) {
    const auto physical = FILE_SYSTEM.resolvePhysicalPath(path);
    if (!physical) {
        statusMessage_ = "Cannot resolve: " + path.string();
        return;
    }
#if defined(_WIN32)
    // Open the parent with the entry preselected.
    const std::wstring parameters = L"/select,\"" + physical->wstring() + L"\"";
    if (ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr,
                      SW_SHOWNORMAL) <= reinterpret_cast<HINSTANCE>(32))
        statusMessage_ = "Cannot open Explorer";
#endif
}

void ProjectPanel::copyToClipboard(const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
}

void ProjectPanel::requestDelete(const VirtualPath& path) {
    deleteTarget_ = path;
    deleteDependentCount_ = model_.dependentsOf(path).size();
    deleteModalOpen_ = true;
    deleteModalPending_ = true;
}

} // namespace engine::editor
