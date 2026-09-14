#include "tools/editor/HierarchyPanel.h"

#include "imgui.h"
#include "scene/scene/Scene.h"
#include "scene/node/Node.h"
#include "tools/editor/SceneDocument.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace engine::editor {

namespace {

constexpr const char* kNodePayload = "MINI_SCENE_NODE";
struct NodePayload {
    std::uint64_t revision;
    NodeHandle node;
};

int resizeRenameBuffer(ImGuiInputTextCallbackData* data) {
    auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
    buffer.resize(static_cast<std::size_t>(data->BufSize));
    data->Buf = buffer.data();
    return 0;
}

} // namespace

void HierarchyPanel::syncDocument() {
    if (revision_ != document_.revision() || !document_.valid()) {
        revision_ = document_.revision();
        selection_ = {};
        renameTarget_ = {};
        renameBuffer_.clear();
        focusRename_ = false;
        pending_.reset();
        expand_.clear();
        hoverTarget_ = {};
        statusMessage_.clear();
    }
    if (document_.valid()) {
        if (!document_.scene().findNode(selection_))
            selection_ = {};
        if (!document_.scene().findNode(renameTarget_))
            renameTarget_ = {};
    }
}

void HierarchyPanel::draw() {
    syncDocument();
    if (!ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse) || !document_.valid()) {
        ImGui::End();
        return;
    }
    // ID 不依赖节点名称或祖先链，重命名与换父节点都不会丢失展开状态。
    ImGui::PushID(static_cast<int>(revision_ >> 32U));
    ImGui::PushID(static_cast<int>(revision_));
    hoverSeen_ = false;
    const NodeHandle root = document_.scene().rootHandle();
    drawNode(root);

    if (!statusMessage_.empty())
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##root-drop",
                           ImVec2{std::max(available.x, 1.0F), std::max(available.y, 32.0F)});
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selection_ = {};
    if (drawDropTarget(root, min, max, true) != Drop::None)
        ImGui::GetWindowDrawList()->AddLine(
            min, ImVec2{max.x, min.y}, ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0F);
    if (ImGui::BeginPopupContextItem("##empty-menu")) {
        if (ImGui::MenuItem("Create Empty"))
            pending_ = Request{Action::Create, revision_, {}, root};
        ImGui::EndPopup();
    }
    // 状态文字附近等未被行控件覆盖的空白也提供根层创建菜单。
    if (ImGui::BeginPopupContextWindow("##background-menu",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty"))
            pending_ = Request{Action::Create, revision_, {}, root};
        ImGui::EndPopup();
    }

    if (!hoverSeen_)
        hoverTarget_ = {};
    if (const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        payload && payload->IsDataType(kNodePayload) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        const float top = ImGui::GetWindowPos().y + ImGui::GetFrameHeight();
        const float bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        const float mouse = ImGui::GetIO().MousePos.y;
        const float direction =
            mouse < top + 24.0F ? -1.0F : (mouse > bottom - 24.0F ? 1.0F : 0.0F);
        if (direction != 0.0F)
            ImGui::SetScrollY(
                std::clamp(ImGui::GetScrollY() + direction * 350.0F * ImGui::GetIO().DeltaTime,
                           0.0F,
                           ImGui::GetScrollMaxY()));
    }
    ImGui::PopID();
    ImGui::PopID();
    ImGui::End();
    applyRequest();
}

void HierarchyPanel::drawNode(NodeHandle handle) {
    const Node* node = document_.scene().findNode(handle);
    if (!node)
        return;
    const bool root = handle == document_.scene().rootHandle();
    ImGui::PushID(static_cast<int>(handle.index));
    ImGui::PushID(static_cast<int>(handle.generation));
    const auto expand = std::ranges::find(expand_, handle);
    if (expand != expand_.end()) {
        ImGui::SetNextItemOpen(true);
        expand_.erase(expand);
    } else if (hoverTarget_ == handle && ImGui::GetTime() - hoverSince_ >= 0.6) {
        ImGui::SetNextItemOpen(true);
    }
    ImGui::SetNextItemStorageID(ImGui::GetID("##node"));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selection_ == handle)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (root)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    else if (node->children().empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    const std::string label = root ? "Scene Root" : std::string{node->name()};
    if (!node->activeInHierarchy())
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool opened = ImGui::TreeNodeEx("##node", flags, "%s", label.c_str());
    if (!node->activeInHierarchy())
        ImGui::PopStyleColor();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selection_ = handle;
        if (!root && !ImGui::IsItemToggledOpen() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            beginRename(handle);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        selection_ = handle;
    if (!root && renameTarget_ != handle &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_PayloadAutoExpire)) {
        const NodePayload payload{revision_, handle};
        ImGui::SetDragDropPayload(kNodePayload, &payload, sizeof(payload));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
    const Drop drop = drawDropTarget(handle, min, max, root);
    drawContextMenu(handle);
    drawRename(handle);
    ImGui::PopID();
    ImGui::PopID();

    if (opened) {
        ImGui::Indent();
        for (const NodeHandle child : node->children())
            drawNode(child);
        ImGui::Unindent();
    }
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
    if (drop == Drop::Into)
        ImGui::GetWindowDrawList()->AddRect(min, max, color, 0.0F, 0, 2.0F);
    else if (drop != Drop::None) {
        const float y =
            drop == Drop::Before
                ? min.y
                : std::max(max.y, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);
        ImGui::GetWindowDrawList()->AddLine(ImVec2{min.x, y}, ImVec2{max.x, y}, color, 2.0F);
    }
}

HierarchyPanel::Drop HierarchyPanel::drawDropTarget(NodeHandle handle,
                                                    const ImVec2& min,
                                                    const ImVec2& max,
                                                    bool rootTarget) {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(kNodePayload) || payload->DataSize != sizeof(NodePayload))
        return Drop::None;
    NodePayload source;
    std::memcpy(&source, payload->Data, sizeof(source));
    if (source.revision != revision_ || !ImGui::BeginDragDropTarget())
        return Drop::None;

    Scene& scene = document_.scene();
    const Node* target = scene.findNode(handle);
    const Node* node = scene.findNode(source.node);
    Drop drop = Drop::Into;
    const float fraction = (ImGui::GetIO().MousePos.y - min.y) / std::max(max.y - min.y, 1.0F);
    if (!rootTarget)
        drop = fraction < 0.25F ? Drop::Before : (fraction > 0.75F ? Drop::After : Drop::Into);
    const NodeHandle parent =
        drop == Drop::Into ? handle : (target ? target->parent() : NodeHandle{});
    const Node* parentNode = scene.findNode(parent);
    std::size_t index = 0;
    if (parentNode) {
        for (const NodeHandle child : parentNode->children()) {
            if (drop != Drop::Into && child == handle) {
                if (drop == Drop::After && child != source.node)
                    ++index;
                break;
            }
            if (child != source.node)
                ++index;
        }
    }
    std::string error;
    const bool valid = target && node && scene.canMoveNode(source.node, parent, index, error);
    if (valid) {
        const char* placement =
            drop == Drop::Into ? "As last child of" : (drop == Drop::Before ? "Before" : "After");
        ImGui::SetTooltip("%s\n%s %s",
                          std::string{node->name()}.c_str(),
                          placement,
                          rootTarget ? "Scene Root" : std::string{target->name()}.c_str());
        if (drop == Drop::Into && !rootTarget) {
            hoverSeen_ = true;
            if (hoverTarget_ != handle) {
                hoverTarget_ = handle;
                hoverSince_ = ImGui::GetTime();
            }
        }
        if (const ImGuiPayload* accepted =
                ImGui::AcceptDragDropPayload(kNodePayload,
                                             ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                 ImGuiDragDropFlags_AcceptNoDrawDefaultRect |
                                                 ImGuiDragDropFlags_AcceptNoPreviewTooltip);
            accepted && accepted->IsDelivery()) {
            pending_ = Request{Action::Move, source.revision, source.node, parent, index};
        }
    } else {
        ImGui::SetTooltip(
            "%s", error.empty() ? "The dragged node is no longer available." : error.c_str());
    }
    ImGui::EndDragDropTarget();
    return valid ? drop : Drop::None;
}

void HierarchyPanel::drawContextMenu(NodeHandle handle) {
    if (!ImGui::BeginPopupContextItem("##node-menu"))
        return;
    if (ImGui::MenuItem("Create Empty Child"))
        pending_ = Request{Action::Create, revision_, {}, handle};
    if (handle != document_.scene().rootHandle()) {
        if (ImGui::MenuItem("Rename"))
            beginRename(handle);
        if (ImGui::MenuItem("Delete"))
            pending_ = Request{Action::Delete, revision_, handle};
    }
    ImGui::EndPopup();
}

void HierarchyPanel::beginRename(NodeHandle handle) {
    const Node* node = document_.scene().findNode(handle);
    if (!node || handle == document_.scene().rootHandle())
        return;
    renameTarget_ = handle;
    const auto name = node->name();
    renameBuffer_.assign(std::max<std::size_t>(256, name.size() + 1), '\0');
    std::memcpy(renameBuffer_.data(), name.data(), name.size());
    focusRename_ = true;
}

void HierarchyPanel::drawRename(NodeHandle handle) {
    if (renameTarget_ != handle)
        return;
    if (focusRename_)
        ImGui::SetKeyboardFocusHere();
    focusRename_ = false;
    ImGui::SetNextItemWidth(-1.0F);
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
        Node* node = document_.scene().findNode(handle);
        if (node && renameBuffer_[0] != '\0' && node->name() != renameBuffer_.data()) {
            node->setName(renameBuffer_.data());
            document_.markDirty();
        }
        renameTarget_ = {};
    }
}

void HierarchyPanel::expandAncestors(NodeHandle handle) {
    for (const Node* node = document_.scene().findNode(handle); node;
         node = document_.scene().findNode(node->parent())) {
        if (std::ranges::find(expand_, node->handle()) == expand_.end())
            expand_.push_back(node->handle());
    }
}

void HierarchyPanel::applyRequest() {
    if (!pending_)
        return;
    const Request request = *pending_;
    pending_.reset();
    if (request.revision != document_.revision() || !document_.valid())
        return;
    Scene& scene = document_.scene();
    statusMessage_.clear();
    if (request.action == Action::Create) {
        if (!scene.findNode(request.parent))
            return;
        const NodeHandle created = scene.createNode("Node");
        Node* node = scene.findNode(created);
        if (!node)
            return;
        if (!node->setParent(request.parent)) {
            (void)scene.destroyNode(created);
            return;
        }
        selection_ = created;
        expandAncestors(request.parent);
        beginRename(created);
        document_.markDirty();
    } else if (request.action == Action::Delete) {
        const Node* node = scene.findNode(request.node);
        if (!node)
            return;
        const NodeHandle parent = node->parent();
        if (scene.destroyNode(request.node)) {
            selection_ = parent;
            renameTarget_ = {};
            document_.markDirty();
        }
    } else {
        const NodeMoveResult result =
            scene.moveNode(request.node, request.parent, request.index, statusMessage_);
        if (result != NodeMoveResult::Rejected) {
            selection_ = request.node;
            expandAncestors(request.parent);
            if (result == NodeMoveResult::Changed)
                document_.markDirty();
        }
    }
}

} // namespace engine::editor
