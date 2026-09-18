#include "tools/editor/widgets/EditorWidgets.h"

#include "asset/database/AssetDatabase.h"

#include <algorithm>

namespace engine::editor {
namespace {

// imgui_internal.h's ImGuiItemFlags_MixedValue; the editor code does not
// include the internal header, so the flag value is spelled out.
constexpr int kItemFlagsMixedValue = 1 << 12;

} // namespace

bool dragVec3(const char* label, math::Vec3& value) {
    return ImGui::DragFloat3(label, &value.x, kDragSpeed, 0.0F, 0.0F, "%.3f");
}

bool dragEuler(const char* label, math::Quat& rotation) {
    // math::degrees/radians are scalar helpers; glm provides the vector overloads.
    math::Vec3 euler = glm::degrees(math::toEuler(rotation));
    if (!ImGui::DragFloat3(label, &euler.x, 0.5F, -360.0F, 360.0F, "%.1f deg"))
        return false;
    rotation = math::normalize(math::fromEuler(glm::radians(euler)));
    return true;
}

bool inputUint(const char* label, std::uint32_t& value) {
    int temporary = static_cast<int>(value);
    if (!ImGui::InputInt(label, &temporary, 0, 0))
        return false;
    value = temporary > 0 ? static_cast<std::uint32_t>(temporary) : 0U;
    return true;
}

int resizeRenameBuffer(ImGuiInputTextCallbackData* data) {
    auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
    buffer.resize(static_cast<std::size_t>(data->BufSize));
    data->Buf = buffer.data();
    return 0;
}

std::vector<VirtualPath> collectAssets(AssetType type) {
    std::vector<VirtualPath> result;
    for (const AssetRecord& record : ASSET_DATABASE.records()) {
        if (record.type == type)
            result.push_back(record.sourcePath);
    }
    std::ranges::sort(result, {}, [](const VirtualPath& path) {
        return path.relativePath();
    });
    return result;
}

bool assetCombo(const char* label,
                const std::vector<VirtualPath>& paths,
                const VirtualPath& current,
                VirtualPath& chosenPath,
                const char* noneLabel) {
    const std::string currentLabel =
        current.valid() ? current.relativePath() : std::string{noneLabel ? noneLabel : "(none)"};
    chosenPath = {};
    if (!ImGui::BeginCombo(label, currentLabel.c_str()))
        return false;
    bool picked = false;
    if (noneLabel != nullptr && ImGui::Selectable(noneLabel, !current.valid())) {
        picked = true; // chosenPath stays invalid: the caller clears the slot.
    } else if (noneLabel != nullptr && !current.valid()) {
        ImGui::SetItemDefaultFocus();
    }
    for (const VirtualPath& path : paths) {
        const bool isSelected = path == current;
        if (ImGui::Selectable(path.relativePath().c_str(), isSelected)) {
            chosenPath = path;
            picked = true;
        }
        if (isSelected)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return picked;
}

bool mixedCheckbox(const char* label, bool anyTrue, bool anyFalse, bool& active) {
    if (!anyTrue || !anyFalse) {
        active = anyTrue;
        return ImGui::Checkbox(label, &active);
    }
    // Mixed group: the square renders the mixed-value block, the local value
    // starts false so the first click unifies the whole group to active.
    ImGui::PushItemFlag(kItemFlagsMixedValue, true);
    active = false;
    const bool clicked = ImGui::Checkbox(label, &active);
    ImGui::PopItemFlag();
    return clicked;
}

} // namespace engine::editor
