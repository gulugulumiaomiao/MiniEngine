#include "tools/editor/ProjectPanel.h"

#include "asset/base/Asset.h"
#include "core/filesystem/FileSystem.h"

#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace engine::editor {
namespace {

struct ProjectEntry {
    VirtualPath path;
    std::string name;
    bool directory{};
};

AssetType assetTypeOf(const VirtualPath& path) {
    // std::filesystem::path::extension() only yields the final suffix (.json), so asset
    // types are recognized through the full filename.
    const std::string name = path.filename();
    if (name.ends_with(".scene.json"))
        return AssetType::Scene;
    if (name.ends_with(".material.json"))
        return AssetType::Material;
    if (name.ends_with(".mesh.json"))
        return AssetType::Mesh;
    if (name.ends_with(".shader.json"))
        return AssetType::Shader;
    if (name.ends_with(".png") || name.ends_with(".jpg") || name.ends_with(".jpeg") ||
        name.ends_with(".tga"))
        return AssetType::Texture;
    return AssetType::Unknown;
}

bool entryOrder(const ProjectEntry& left, const ProjectEntry& right) {
    if (left.directory != right.directory)
        return left.directory;
    return left.name < right.name;
}

} // namespace

void ProjectPanel::draw() {
    if (!ImGui::Begin("Project", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    drawDirectory(VirtualPath{"assets://"});
    ImGui::End();
}

void ProjectPanel::drawDirectory(const VirtualPath& directory) {
    const std::vector<VirtualPath> files = FILE_SYSTEM.listFiles(directory, false);
    std::vector<ProjectEntry> entries;
    entries.reserve(files.size());
    for (const VirtualPath& file : files) {
        const std::string name = file.filename();
        if (name.size() > 5 && name.ends_with(".meta"))
            continue;
        // listFiles returns files only; reconstruct subdirectories from child paths so
        // empty directories remain visible as collapsed nodes.
        const std::string relative = file.relativePath();
        const std::size_t separator = relative.find('/');
        if (separator == std::string::npos) {
            entries.push_back({file, name, false});
            continue;
        }
        const std::string directoryName = relative.substr(0, separator);
        const VirtualPath childDirectory = directory.joined(directoryName);
        if (std::ranges::any_of(entries, [&](const ProjectEntry& entry) {
                return entry.directory && entry.name == directoryName;
            })) {
            continue;
        }
        entries.push_back({childDirectory, directoryName, true});
    }
    std::ranges::sort(entries, entryOrder);

    for (const ProjectEntry& entry : entries) {
        if (entry.directory) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.72F, 0.78F, 0.86F, 1.0F});
            const bool open = ImGui::TreeNode(entry.name.c_str());
            ImGui::PopStyleColor();
            if (open) {
                drawDirectory(entry.path);
                ImGui::TreePop();
            }
            continue;
        }

        const AssetType type = assetTypeOf(entry.path);
        const char* icon = "  ";
        switch (type) {
        case AssetType::Scene: icon = "S "; break;
        case AssetType::Material: icon = "M "; break;
        case AssetType::Mesh: icon = "T "; break;
        case AssetType::Shader: icon = "R "; break;
        case AssetType::Texture: icon = "P "; break;
        default: break;
        }
        const std::string label = std::string{icon} + entry.name;
        if (type == AssetType::Scene) {
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && openScene_)
                    openScene_(entry.path);
            }
        } else {
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_Disabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", entry.path.string().c_str());
        }
    }
}

} // namespace engine::editor
