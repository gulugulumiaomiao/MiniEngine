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
// 拖动 payload 必须自包含：ImGui 只做浅拷贝，vector 成员会悬空，所以用固定容量数组。
constexpr std::size_t kMaxDragNodes = 256;
struct NodePayload {
    std::uint64_t revision;
    // 0 表示多选超出拖动上限，接收端拒绝整组。
    std::uint32_t count;
    NodeHandle nodes[kMaxDragNodes];
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
        selection_.clear();
        dragStartSelection_.clear();
        renameTarget_ = {};
        renameBuffer_.clear();
        focusRename_ = false;
        pending_.reset();
        expand_.clear();
        hoverTarget_ = {};
        statusMessage_.clear();
    }
    if (document_.valid()) {
        selection_.removeIf(
            [this](NodeHandle handle) { return !document_.scene().findNode(handle); });
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
    visibleNodes_.clear();
    const NodeHandle root = document_.scene().rootHandle();
    drawNode(root);

    if (!statusMessage_.empty())
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##root-drop",
                           ImVec2{std::max(available.x, 1.0F), std::max(available.y, 32.0F)});
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selection_.clear();
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
    visibleNodes_.push_back(handle);
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
    if (selection_.contains(handle))
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
        const bool ctrl = ImGui::GetIO().KeyCtrl, shift = ImGui::GetIO().KeyShift;
        if (ctrl || shift) {
            selection_.click(handle, ctrl, shift, visibleNodes_);
            // 修饰键按下是在调整选择，调整后的集合就是随后的拖动组。
            dragStartSelection_ = selection_;
        } else {
            // 无修饰按下会替换选择；点到组内成员时，随后的拖动携带替换前的整组。
            dragStartSelection_ = selection_;
            selection_.click(handle, false, false, visibleNodes_);
        }
        if (!root && !ctrl && !shift && !ImGui::IsItemToggledOpen() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            beginRename(handle);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        // 右键未选中的节点先单选；已处于多选中则保留整组选择弹批量菜单。
        if (!selection_.contains(handle) || root)
            selection_.select(handle);
    }
    if (!root && renameTarget_ != handle &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_PayloadAutoExpire)) {
        std::vector<NodeHandle> sources;
        if (dragStartSelection_.contains(handle))
            sources = dragStartSelection_.items();
        else
            sources = {handle};
        // 按可见行顺序稳定排序，批量插入时保持树中的相对顺序；折叠的选中项排在末尾。
        const auto visibleIndex = [this](NodeHandle candidate) {
            const auto it = std::ranges::find(visibleNodes_, candidate);
            return it != visibleNodes_.end() ? static_cast<std::size_t>(it - visibleNodes_.begin())
                                             : visibleNodes_.size();
        };
        std::stable_sort(sources.begin(), sources.end(), [&](NodeHandle lhs, NodeHandle rhs) {
            return visibleIndex(lhs) < visibleIndex(rhs);
        });
        NodePayload payload{revision_, 0, {}};
        if (sources.size() <= kMaxDragNodes) {
            payload.count = static_cast<std::uint32_t>(sources.size());
            std::copy(sources.begin(), sources.end(), payload.nodes);
        }
        ImGui::SetDragDropPayload(kNodePayload, &payload, sizeof(payload));
        if (sources.size() > 1) {
            ImGui::Text("%s + %u more", label.c_str(), static_cast<unsigned>(sources.size() - 1));
            // 拖动整组：恢复无修饰按下时被替换掉的多选。
            selection_ = dragStartSelection_;
        } else {
            ImGui::TextUnformatted(label.c_str());
        }
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
    if (source.count == 0) {
        ImGui::SetTooltip("Too many nodes are selected to drag (limit %u).",
                          static_cast<unsigned>(kMaxDragNodes));
        ImGui::EndDragDropTarget();
        return Drop::None;
    }

    Scene& scene = document_.scene();
    const Node* target = scene.findNode(handle);
    const auto isSource = [&source](NodeHandle candidate) {
        return std::find(source.nodes, source.nodes + source.count, candidate) !=
               source.nodes + source.count;
    };
    Drop drop = Drop::Into;
    const float fraction = (ImGui::GetIO().MousePos.y - min.y) / std::max(max.y - min.y, 1.0F);
    if (!rootTarget)
        drop = fraction < 0.25F ? Drop::Before : (fraction > 0.75F ? Drop::After : Drop::Into);
    const NodeHandle parent =
        drop == Drop::Into ? handle : (target ? target->parent() : NodeHandle{});
    const Node* parentNode = scene.findNode(parent);
    // 插入位在排除全部拖动节点后的同级列表上计算，之后逐个以 index、index+1… 插入，
    // 源集合最终按可见顺序占据连续位置。
    std::size_t index = 0;
    if (parentNode) {
        for (const NodeHandle child : parentNode->children()) {
            if (drop != Drop::Into && child == handle) {
                if (drop == Drop::After && !isSource(child))
                    ++index;
                break;
            }
            if (!isSource(child))
                ++index;
        }
    }
    std::string error;
    bool valid = target != nullptr && !isSource(handle);
    if (valid) {
        for (std::uint32_t i = 0; i < source.count; ++i) {
            if (!scene.findNode(source.nodes[i]) ||
                !scene.canMoveNode(source.nodes[i], parent, index, error)) {
                valid = false;
                break;
            }
        }
    } else if (target != nullptr) {
        error = "The drop target is part of the dragged nodes.";
    }
    if (valid) {
        const char* placement =
            drop == Drop::Into
                ? (source.count > 1 ? "As last children of" : "As last child of")
                : (drop == Drop::Before ? "Before" : "After");
        if (source.count > 1) {
            ImGui::SetTooltip("%u nodes\n%s %s",
                              static_cast<unsigned>(source.count),
                              placement,
                              rootTarget ? "Scene Root" : std::string{target->name()}.c_str());
        } else {
            const Node* node = scene.findNode(source.nodes[0]);
            ImGui::SetTooltip("%s\n%s %s",
                              std::string{node->name()}.c_str(),
                              placement,
                              rootTarget ? "Scene Root" : std::string{target->name()}.c_str());
        }
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
            pending_ = Request{Action::Move,
                               source.revision,
                               std::vector<NodeHandle>(source.nodes, source.nodes + source.count),
                               parent,
                               index};
        }
    } else {
        ImGui::SetTooltip(
            "%s", error.empty() ? "The dragged nodes are no longer available." : error.c_str());
    }
    ImGui::EndDragDropTarget();
    return valid ? drop : Drop::None;
}

void HierarchyPanel::drawContextMenu(NodeHandle handle) {
    if (!ImGui::BeginPopupContextItem("##node-menu"))
        return;
    const bool root = handle == document_.scene().rootHandle();
    const bool multi = !root && selection_.size() > 1 && selection_.contains(handle);
    if (multi) {
        if (ImGui::MenuItem("Delete Selected"))
            pending_ = Request{Action::Delete, revision_, selection_.items()};
        if (ImGui::MenuItem("Activate Selected"))
            pending_ = Request{Action::SetActive, revision_, selection_.items(), {}, 0, true};
        if (ImGui::MenuItem("Deactivate Selected"))
            pending_ = Request{Action::SetActive, revision_, selection_.items(), {}, 0, false};
    } else {
        if (ImGui::MenuItem("Create Empty Child"))
            pending_ = Request{Action::Create, revision_, {}, handle};
        if (!root) {
            if (ImGui::MenuItem("Rename"))
                beginRename(handle);
            if (ImGui::MenuItem("Delete"))
                pending_ = Request{Action::Delete, revision_, {handle}};
        }
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
        selection_.select(created);
        expandAncestors(request.parent);
        beginRename(created);
        document_.markDirty();
    } else if (request.action == Action::Delete) {
        applyDelete(request.nodes);
    } else if (request.action == Action::SetActive) {
        applySetActive(request.nodes, request.active);
    } else {
        if (request.nodes.empty())
            return;
        const Node* parentNode = scene.findNode(request.parent);
        if (!parentNode)
            return;
        // 锚点集合：排除全部源后的同级列表中，插入位之前的前缀元素。
        std::vector<NodeHandle> prefix;
        {
            std::size_t remaining = request.index;
            for (const NodeHandle child : parentNode->children()) {
                if (remaining == 0)
                    break;
                if (std::find(request.nodes.begin(), request.nodes.end(), child) ==
                    request.nodes.end()) {
                    prefix.push_back(child);
                    --remaining;
                }
            }
        }
        // 逐个插入：每个源插在“最后一个锚点（前缀元素或已插入源）之后”。中间态里
        // 尚未处理的源仍占据原位，所以插入位必须在当前真实列表上动态定位。
        bool rejected = false;
        bool anyChanged = false;
        for (std::size_t i = 0; i < request.nodes.size(); ++i) {
            const auto& children = scene.findNode(request.parent)->children();
            std::size_t insertIndex = 0;
            std::size_t position = 0;
            for (const NodeHandle child : children) {
                if (child == request.nodes[i])
                    continue; // moveNode 会先移除该源，不计入位置
                const bool anchor =
                    std::find(prefix.begin(), prefix.end(), child) != prefix.end() ||
                    std::find(request.nodes.begin(), request.nodes.begin() + i, child) !=
                        request.nodes.begin() + i;
                if (anchor)
                    insertIndex = position + 1;
                ++position;
            }
            const NodeMoveResult result =
                scene.moveNode(request.nodes[i], request.parent, insertIndex, statusMessage_);
            if (result == NodeMoveResult::Rejected) {
                rejected = true;
                break;
            }
            if (result == NodeMoveResult::Changed)
                anyChanged = true;
        }
        if (anyChanged)
            document_.markDirty();
        if (!rejected) {
            // 单节点拖动把选择聚焦到被移动节点；批量拖动整组保持选中（句柄不变）。
            if (request.nodes.size() == 1)
                selection_.select(request.nodes.front());
            expandAncestors(request.parent);
        }
    }
}

void HierarchyPanel::applyDelete(const std::vector<NodeHandle>& nodes) {
    Scene& scene = document_.scene();
    NodeHandle lastParent{};
    bool anyDeleted = false;
    for (const NodeHandle handle : nodes) {
        const Node* node = scene.findNode(handle);
        // Scene Root 不可删除；父级先被销毁时子级句柄已失效，自然跳过。
        if (!node || handle == scene.rootHandle())
            continue;
        lastParent = node->parent();
        if (scene.destroyNode(handle))
            anyDeleted = true;
    }
    if (!anyDeleted)
        return;
    // 清掉失效句柄；全部删光时选中原父节点，与单删行为一致。
    selection_.removeIf([&scene](NodeHandle handle) { return !scene.findNode(handle); });
    if (selection_.empty())
        selection_.select(lastParent);
    renameTarget_ = {};
    document_.markDirty();
}

void HierarchyPanel::applySetActive(const std::vector<NodeHandle>& nodes, bool active) {
    Scene& scene = document_.scene();
    bool anyChanged = false;
    for (const NodeHandle handle : nodes) {
        if (Node* node = scene.findNode(handle)) {
            node->setActive(active);
            anyChanged = true;
        }
    }
    if (anyChanged)
        document_.markDirty();
}

} // namespace engine::editor
