#include "tools/editor/ProjectPickerPanel.h"

#if defined(MINI_EDITOR)

#include "core/logging/Log.h"
#include "runtime/config/ProjectConfig.h"
#include "tools/editor/ProjectTemplate.h"

#include "imgui.h"

#include <Windows.h>
#include <shlobj.h>

#include <cstring>

namespace engine::editor {

ProjectPickerPanel::ProjectPickerPanel(ProjectRegistry& registry)
    : registry_(registry) {
    newProjectName_[0] = '\0';
}

bool ProjectPickerPanel::draw() {
    if (!visible_)
        return false;

    ImGui::OpenPopup("ProjectPicker");
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{520, 420}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("ProjectPicker", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        visible_ = false;
        return false;
    }

    if (showNewProjectForm_) {
        drawNewProjectForm();
    } else {
        drawProjectList();
    }

    if (!statusMessage_.empty()) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.9F, 0.4F, 0.4F, 1.0F});
        ImGui::TextWrapped("%s", statusMessage_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndPopup();
    return true;
}

void ProjectPickerPanel::drawProjectList() {
    ImGui::TextUnformatted("Recent Projects");
    ImGui::Separator();

    if (registry_.empty()) {
        ImGui::TextDisabled("No recent projects. Create or browse for one.");
    }

    for (std::size_t i = 0; i < registry_.size(); ++i) {
        const RecentProjectEntry& entry = registry_.entries()[i];
        ImGui::PushID(static_cast<int>(i));

        // The whole row is one click target for opening the project.
        const std::string label = entry.name + "##" + entry.rootDirectory.string();
        if (ImGui::Button(label.c_str(),
                          ImVec2{ImGui::GetContentRegionAvail().x, 0.0F})) {
            if (openHandler_) {
                openHandler_(entry.rootDirectory);
                visible_ = false;
                statusMessage_.clear();
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", entry.rootDirectory.string().c_str());

        bool removed = false;
        if (ImGui::Button("Open")) {
            if (openHandler_) {
                openHandler_(entry.rootDirectory);
                visible_ = false;
                statusMessage_.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            registry_.removeEntry(i);
            notifyChanged();
            removed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete...")) {
            std::string error;
            if (!registry_.deleteFromDisk(i, true)) {
                statusMessage_ = "Delete failed for: " + entry.name;
            } else {
                notifyChanged();
                statusMessage_.clear();
            }
            removed = true;
        }

        ImGui::PopID();
        if (removed) {
            --i;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("New Project...")) {
        showNewProjectForm_ = true;
        newProjectName_[0] = '\0';
        newProjectParent_.clear();
        statusMessage_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse Folder...")) {
        openFolderPicker();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        visible_ = false;
    }
}

void ProjectPickerPanel::drawNewProjectForm() {
    ImGui::TextUnformatted("Create New Project");
    ImGui::Separator();

    ImGui::InputText("Project Name", newProjectName_, sizeof(newProjectName_));

    if (newProjectParent_.empty()) {
        ImGui::TextDisabled("Location: (not selected)");
    } else {
        ImGui::Text("Location: %s", newProjectParent_.string().c_str());
    }
    if (ImGui::Button("Browse...")) {
        const std::filesystem::path picked = openNativeFolderPicker();
        if (!picked.empty())
            newProjectParent_ = picked;
    }

    ImGui::Separator();
    if (ImGui::Button("Create")) {
        if (newProjectName_[0] == '\0') {
            statusMessage_ = "Please enter a project name";
        } else if (newProjectParent_.empty()) {
            statusMessage_ = "Please select a location";
        } else {
            std::string error;
            const auto result =
                createNewProject(newProjectParent_, newProjectName_, error);
            if (!result) {
                statusMessage_ = "Create failed: " + error;
            } else if (createHandler_) {
                createHandler_(newProjectParent_, newProjectName_);
                showNewProjectForm_ = false;
                visible_ = false;
                statusMessage_.clear();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Back")) {
        showNewProjectForm_ = false;
        statusMessage_.clear();
    }
}

void ProjectPickerPanel::openFolderPicker() {
    const std::filesystem::path picked = openNativeFolderPicker();
    if (!picked.empty() && openHandler_) {
        openHandler_(picked);
        visible_ = false;
        statusMessage_.clear();
    }
}

void ProjectPickerPanel::notifyChanged() {
    if (changedHandler_)
        changedHandler_();
}

std::filesystem::path openNativeFolderPicker() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool alreadyInitialized = (hr == RPC_E_CHANGED_MODE);
    if (FAILED(hr) && !alreadyInitialized)
        return {};

    std::filesystem::path result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog)))) {
        DWORD options;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_PICKFOLDERS);
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = std::filesystem::path{path};
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    if (!alreadyInitialized)
        CoUninitialize();
    return result;
}

} // namespace engine::editor

#endif // MINI_EDITOR
