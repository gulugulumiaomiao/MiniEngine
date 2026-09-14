#include "tools/editor/InspectorPanel.h"

#include "core/filesystem/FileSystem.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/scene/Lighting.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "scene/node/Node.h"
#include "scene/scene/Scene.h"
#include "tools/editor/SceneDocument.h"

#include "imgui.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace engine::editor {
namespace {

constexpr float kDragSpeed = 0.05F;

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

// Combo over asset paths. Returns the index of the chosen path, or -1 when nothing was
// picked this frame.
int assetCombo(const char* label, const std::vector<VirtualPath>& paths,
               const VirtualPath& current) {
    const std::string currentLabel =
        current.valid() ? current.relativePath() : std::string{"(none)"};
    if (!ImGui::BeginCombo(label, currentLabel.c_str()))
        return -1;
    int chosen = -1;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const bool isSelected = paths[index] == current;
        if (ImGui::Selectable(paths[index].relativePath().c_str(), isSelected))
            chosen = static_cast<int>(index);
        if (isSelected)
            ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return chosen;
}

} // namespace

void InspectorPanel::draw(const SelectionSet<NodeHandle>& selection) {
    if (!ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    statusMessage_.clear();
    if (!document_.valid()) {
        ImGui::TextUnformatted("No Scene document is open");
        ImGui::End();
        return;
    }

    if (selection.empty()) {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::End();
        return;
    }

    if (selection.size() == 1) {
        Node* node = document_.scene().findNode(selection.primary());
        if (node == nullptr) {
            ImGui::TextUnformatted("Nothing selected");
            ImGui::End();
            return;
        }

        drawNodeHeader(*node);
        drawTransform(*node);
        drawMesh(*node);
        drawMaterial(*node);
        drawCamera(*node);
        drawLight(*node);
        drawAddComponent(*node);
    } else {
        // 多选视图只展示共有字段；编辑同步写入所有选中节点。
        drawMultiHeader(selection);
        drawMultiActive(selection);
        drawMultiTransform(selection);
    }

    if (!statusMessage_.empty())
        ImGui::TextUnformatted(statusMessage_.c_str());
    ImGui::End();
}

std::vector<Node*> InspectorPanel::collectNodes(const SelectionSet<NodeHandle>& selection) const {
    std::vector<Node*> nodes;
    for (const NodeHandle handle : selection.items())
        if (Node* node = document_.scene().findNode(handle))
            nodes.push_back(node);
    return nodes;
}

void InspectorPanel::drawMultiHeader(const SelectionSet<NodeHandle>& selection) {
    ImGui::Text("%u nodes selected", static_cast<unsigned>(selection.size()));
    ImGui::Separator();
}

void InspectorPanel::drawMultiActive(const SelectionSet<NodeHandle>& selection) {
    const std::vector<Node*> nodes = collectNodes(selection);
    if (nodes.empty())
        return;
    bool anyActive = false, anyInactive = false;
    for (const Node* node : nodes)
        node->activeSelf() ? anyActive = true : anyInactive = true;
    if (anyActive && anyInactive) {
        // 混合状态：MixedValue 标志让复选框渲染三态方块（数值取自 imgui_internal.h，
        // 编辑器代码不包含内部头）。本地值取 false，首次点击把整组统一为激活，
        // 再次点击统一取消。
        ImGui::PushItemFlag(1 << 12 /* ImGuiItemFlags_MixedValue */, true);
        bool active = false;
        if (ImGui::Checkbox("Active", &active)) {
            for (Node* node : nodes)
                node->setActive(active);
            document_.markDirty();
        }
        ImGui::PopItemFlag();
    } else {
        bool active = anyActive;
        if (ImGui::Checkbox("Active", &active)) {
            for (Node* node : nodes)
                node->setActive(active);
            document_.markDirty();
        }
    }
    ImGui::Separator();
}

void InspectorPanel::drawMultiTransform(const SelectionSet<NodeHandle>& selection) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("Transform");
    const std::vector<Node*> nodes = collectNodes(selection);
    if (nodes.empty()) {
        ImGui::PopID();
        return;
    }
    std::vector<TransformComponent*> transforms;
    transforms.reserve(nodes.size());
    for (Node* node : nodes)
        transforms.push_back(&node->transform());

    // 各组值一致时显示公共值并把编辑写入所有节点；混合值显示灰色占位。
    math::Vec3 position = transforms.front()->localPosition();
    if (std::ranges::all_of(transforms, [&](const TransformComponent* transform) {
            return transform->localPosition() == position;
        })) {
        if (dragVec3("Position", position)) {
            for (TransformComponent* transform : transforms)
                transform->setLocalPosition(position);
            document_.markDirty();
        }
    } else {
        ImGui::TextDisabled("Position  -");
    }

    math::Quat rotation = transforms.front()->localRotation();
    if (std::ranges::all_of(transforms, [&](const TransformComponent* transform) {
            return transform->localRotation() == rotation;
        })) {
        if (dragEuler("Rotation", rotation)) {
            for (TransformComponent* transform : transforms)
                transform->setLocalRotation(rotation);
            document_.markDirty();
        }
    } else {
        ImGui::TextDisabled("Rotation  -");
    }

    math::Vec3 scale = transforms.front()->localScale();
    if (std::ranges::all_of(transforms, [&](const TransformComponent* transform) {
            return transform->localScale() == scale;
        })) {
        if (dragVec3("Scale", scale)) {
            for (TransformComponent* transform : transforms)
                transform->setLocalScale(scale);
            document_.markDirty();
        }
    } else {
        ImGui::TextDisabled("Scale  -");
    }
    ImGui::PopID();
}

void InspectorPanel::drawNodeHeader(Node& node) {
    // InputText needs a writable buffer; a locally sized string doubles as one.
    std::string name{node.name()};
    name.resize(128, char(0));
    if (ImGui::InputText("Name", name.data(), name.size())) {
        node.setName(std::string{name.c_str()});
        document_.markDirty();
    }

    bool active = node.activeSelf();
    if (ImGui::Checkbox("Active", &active)) {
        node.setActive(active);
        document_.markDirty();
    }
    ImGui::Separator();
}

void InspectorPanel::drawTransform(Node& node) {
    TransformComponent& transform = node.transform();
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("Transform");

    math::Vec3 position = transform.localPosition();
    if (dragVec3("Position", position)) {
        transform.setLocalPosition(position);
        document_.markDirty();
    }
    math::Quat rotation = transform.localRotation();
    if (dragEuler("Rotation", rotation)) {
        transform.setLocalRotation(rotation);
        document_.markDirty();
    }
    math::Vec3 scale = transform.localScale();
    if (dragVec3("Scale", scale)) {
        transform.setLocalScale(scale);
        document_.markDirty();
    }

    ImGui::PopID();
}

void InspectorPanel::drawMesh(Node& node) {
    MeshComponent* mesh = node.getComponent<MeshComponent>();
    if (mesh == nullptr)
        return;
    if (!ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("Mesh");

    bool enabled = mesh->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        mesh->setEnabled(enabled);
        document_.markDirty();
    }
    if (ImGui::Checkbox("Visible", &mesh->visible))
        document_.markDirty();
    if (ImGui::Checkbox("Cast Shadow", &mesh->castShadow))
        document_.markDirty();
    if (ImGui::Checkbox("Receive Shadow", &mesh->receiveShadow))
        document_.markDirty();
    if (inputUint("Layer Mask", mesh->layerMask))
        document_.markDirty();

    if (mesh->sourceType() == MeshComponentSourceType::Asset) {
        const std::vector<VirtualPath> meshes = listAssetFiles(".mesh.json");
        const Mesh* current = MESH_MANAGER.find(mesh->mesh());
        const VirtualPath currentPath = current != nullptr ? current->assetPath() : VirtualPath{};
        const int chosen = assetCombo("Mesh Asset", meshes, currentPath);
        if (chosen >= 0) {
            const VirtualPath& target = meshes[static_cast<std::size_t>(chosen)];
            const MeshHandle loaded = MESH_MANAGER.load(target);
            if (loaded) {
                mesh->setAssetMesh(loaded);
                document_.markDirty();
            } else {
                statusMessage_ = "Failed to load mesh: " + target.string();
            }
        }
    } else {
        drawPrimitive(*mesh);
    }

    ImGui::Separator();
    if (ImGui::Button("Remove Component")) {
        node.removeComponent<MeshComponent>();
        document_.markDirty();
    }

    ImGui::PopID();
}

void InspectorPanel::drawPrimitive(MeshComponent& mesh) {
    // Values are edited through local copies and only written back through
    // editPrimitiveRecipe() when a widget actually changed, otherwise the mesh would be
    // rebuilt every frame.
    const MeshBuildRecipe* recipe = mesh.primitiveRecipe();
    if (recipe == nullptr || recipe->parts.empty()) {
        ImGui::TextUnformatted("No primitive recipe");
        return;
    }

    const MeshPrimitivePart& part = recipe->parts.front();
    const char* typeNames[] = {"Plane", "Box", "UV Sphere", "Cylinder"};
    int typeIndex = static_cast<int>(part.primitive.type()) - 1;
    if (ImGui::Combo("Primitive", &typeIndex, typeNames, IM_ARRAYSIZE(typeNames))) {
        MeshBuildRecipe* editable = mesh.editPrimitiveRecipe();
        if (editable != nullptr && !editable->parts.empty()) {
            MeshPrimitivePart& target = editable->parts.front();
            switch (static_cast<MeshPrimitiveType>(typeIndex + 1)) {
            case MeshPrimitiveType::Plane: target.primitive = PlaneGeometry{}; break;
            case MeshPrimitiveType::Box: target.primitive = BoxGeometry{}; break;
            case MeshPrimitiveType::UvSphere: target.primitive = UvSphereGeometry{}; break;
            case MeshPrimitiveType::Cylinder: target.primitive = CylinderGeometry{}; break;
            }
            if (!mesh.applyPrimitiveChanges())
                statusMessage_ = "Failed to rebuild primitive mesh";
            document_.markDirty();
        }
        return;
    }

    bool changed = false;
    if (const auto* plane = std::get_if<PlaneGeometry>(&part.primitive.value)) {
        math::Vec2 size = plane->size;
        std::uint32_t segmentsX = plane->segmentsX;
        std::uint32_t segmentsZ = plane->segmentsZ;
        changed = ImGui::DragFloat2("Size", &size.x, kDragSpeed, 0.01F, 0.0F, "%.3f");
        changed = inputUint("Segments X", segmentsX) || changed;
        changed = inputUint("Segments Z", segmentsZ) || changed;
        if (changed) {
            MeshBuildRecipe* editable = mesh.editPrimitiveRecipe();
            auto* target = std::get_if<PlaneGeometry>(&editable->parts.front().primitive.value);
            if (target) {
                target->size = size;
                target->segmentsX = segmentsX;
                target->segmentsZ = segmentsZ;
            }
        }
    } else if (const auto* box = std::get_if<BoxGeometry>(&part.primitive.value)) {
        math::Vec3 size = box->size;
        std::uint32_t segmentsX = box->segmentsX;
        std::uint32_t segmentsY = box->segmentsY;
        std::uint32_t segmentsZ = box->segmentsZ;
        changed = dragVec3("Size", size);
        changed = inputUint("Segments X", segmentsX) || changed;
        changed = inputUint("Segments Y", segmentsY) || changed;
        changed = inputUint("Segments Z", segmentsZ) || changed;
        if (changed) {
            MeshBuildRecipe* editable = mesh.editPrimitiveRecipe();
            auto* target = std::get_if<BoxGeometry>(&editable->parts.front().primitive.value);
            if (target) {
                target->size = size;
                target->segmentsX = segmentsX;
                target->segmentsY = segmentsY;
                target->segmentsZ = segmentsZ;
            }
        }
    } else if (const auto* sphere = std::get_if<UvSphereGeometry>(&part.primitive.value)) {
        float radius = sphere->radius;
        std::uint32_t longitudeSegments = sphere->longitudeSegments;
        std::uint32_t latitudeSegments = sphere->latitudeSegments;
        changed = ImGui::DragFloat("Radius", &radius, kDragSpeed, 0.01F, 0.0F, "%.3f");
        changed = inputUint("Longitude Segments", longitudeSegments) || changed;
        changed = inputUint("Latitude Segments", latitudeSegments) || changed;
        if (changed) {
            MeshBuildRecipe* editable = mesh.editPrimitiveRecipe();
            auto* target = std::get_if<UvSphereGeometry>(&editable->parts.front().primitive.value);
            if (target) {
                target->radius = radius;
                target->longitudeSegments = longitudeSegments;
                target->latitudeSegments = latitudeSegments;
            }
        }
    } else if (const auto* cylinder = std::get_if<CylinderGeometry>(&part.primitive.value)) {
        float bottomRadius = cylinder->bottomRadius;
        float topRadius = cylinder->topRadius;
        float height = cylinder->height;
        std::uint32_t radialSegments = cylinder->radialSegments;
        std::uint32_t heightSegments = cylinder->heightSegments;
        bool capBottom = cylinder->capBottom;
        bool capTop = cylinder->capTop;
        changed = ImGui::DragFloat("Bottom Radius", &bottomRadius, kDragSpeed, 0.0F, 0.0F, "%.3f");
        changed = ImGui::DragFloat("Top Radius", &topRadius, kDragSpeed, 0.0F, 0.0F, "%.3f") || changed;
        changed = ImGui::DragFloat("Height", &height, kDragSpeed, 0.01F, 0.0F, "%.3f") || changed;
        changed = inputUint("Radial Segments", radialSegments) || changed;
        changed = inputUint("Height Segments", heightSegments) || changed;
        changed = ImGui::Checkbox("Cap Bottom", &capBottom) || changed;
        changed = ImGui::Checkbox("Cap Top", &capTop) || changed;
        if (changed) {
            MeshBuildRecipe* editable = mesh.editPrimitiveRecipe();
            auto* target = std::get_if<CylinderGeometry>(&editable->parts.front().primitive.value);
            if (target) {
                target->bottomRadius = bottomRadius;
                target->topRadius = topRadius;
                target->height = height;
                target->radialSegments = radialSegments;
                target->heightSegments = heightSegments;
                target->capBottom = capBottom;
                target->capTop = capTop;
            }
        }
    }

    if (changed) {
        if (!mesh.applyPrimitiveChanges())
            statusMessage_ = "Failed to rebuild primitive mesh";
        document_.markDirty();
    }
}

void InspectorPanel::drawMaterial(Node& node) {
    MaterialComponent* material = node.getComponent<MaterialComponent>();
    if (material == nullptr)
        return;
    if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("Material");

    bool enabled = material->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        material->setEnabled(enabled);
        document_.markDirty();
    }

    const std::vector<VirtualPath> materials = listAssetFiles(".material.json");
    const std::vector<MaterialHandle> slots = material->materials();
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        ImGui::PushID(static_cast<int>(slot));
        const Material* current = MATERIAL_MANAGER.find(slots[slot]);
        const VirtualPath currentPath = current != nullptr ? current->assetPath() : VirtualPath{};
        const std::string label = "Slot " + std::to_string(slot);
        const int chosen = assetCombo(label.c_str(), materials, currentPath);
        if (chosen >= 0) {
            const VirtualPath& target = materials[static_cast<std::size_t>(chosen)];
            const MaterialHandle loaded = MATERIAL_MANAGER.load(target);
            if (loaded) {
                material->setMaterial(static_cast<std::uint32_t>(slot), loaded);
                document_.markDirty();
            } else {
                statusMessage_ = "Failed to load material: " + target.string();
            }
        }
        ImGui::PopID();
    }

    if (slots.empty())
        ImGui::TextUnformatted("No material slots");
    if (ImGui::Button("Clear Slots")) {
        material->clearMaterials();
        document_.markDirty();
    }

    ImGui::Separator();
    if (ImGui::Button("Remove Component")) {
        node.removeComponent<MaterialComponent>();
        document_.markDirty();
    }

    ImGui::PopID();
}

void InspectorPanel::drawCamera(Node& node) {
    CameraComponent* camera = node.getComponent<CameraComponent>();
    if (camera == nullptr)
        return;
    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("Camera");

    bool enabled = camera->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        camera->setEnabled(enabled);
        document_.markDirty();
    }

    const char* projections[] = {"Perspective", "Orthographic"};
    int projectionIndex = camera->projection == CameraProjection::Perspective ? 0 : 1;
    if (ImGui::Combo("Projection", &projectionIndex, projections, IM_ARRAYSIZE(projections))) {
        camera->projection =
            projectionIndex == 0 ? CameraProjection::Perspective : CameraProjection::Orthographic;
        document_.markDirty();
    }

    if (camera->projection == CameraProjection::Perspective) {
        if (ImGui::DragFloat("Field Of View", &camera->fieldOfView, 0.1F, 1.0F, 179.0F, "%.1f deg"))
            document_.markDirty();
    } else {
        if (ImGui::DragFloat("Orthographic Size", &camera->orthographicSize, 0.05F, 0.01F, 0.0F,
                             "%.2f"))
            document_.markDirty();
    }
    if (ImGui::DragFloat("Near Plane", &camera->nearPlane, 0.01F, 0.001F, 0.0F, "%.3f"))
        document_.markDirty();
    if (ImGui::DragFloat("Far Plane", &camera->farPlane, 0.5F, 0.01F, 0.0F, "%.1f"))
        document_.markDirty();
    if (camera->nearPlane >= camera->farPlane)
        camera->farPlane = camera->nearPlane + 0.1F;
    if (ImGui::ColorEdit4("Clear Color", &camera->clearColor.x))
        document_.markDirty();
    if (inputUint("Culling Mask", camera->cullingMask))
        document_.markDirty();
    if (ImGui::InputInt("Priority", &camera->priority))
        document_.markDirty();
    if (ImGui::Checkbox("Primary", &camera->primary))
        document_.markDirty();

    ImGui::Separator();
    if (ImGui::Button("Remove Component")) {
        node.removeComponent<CameraComponent>();
        document_.markDirty();
    }

    ImGui::PopID();
}

void InspectorPanel::drawLight(Node& node) {
    LightComponent* light = node.getComponent<LightComponent>();
    if (light == nullptr)
        return;
    if (!ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushID("Light");

    bool enabled = light->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        light->setEnabled(enabled);
        document_.markDirty();
    }

    const char* lightTypes[] = {"Directional", "Point", "Spot"};
    int lightTypeIndex = static_cast<int>(light->type);
    if (ImGui::Combo("Type", &lightTypeIndex, lightTypes, IM_ARRAYSIZE(lightTypes))) {
        light->type = static_cast<LightType>(lightTypeIndex);
        document_.markDirty();
    }

    if (ImGui::ColorEdit3("Color", &light->color.x))
        document_.markDirty();
    if (ImGui::DragFloat("Intensity", &light->intensity, 0.05F, 0.0F, 0.0F, "%.2f"))
        document_.markDirty();
    if (light->type != LightType::Directional) {
        if (ImGui::DragFloat("Range", &light->range, 0.1F, 0.01F, 0.0F, "%.1f"))
            document_.markDirty();
    }
    if (light->type == LightType::Spot) {
        if (ImGui::DragFloat("Inner Spot Angle", &light->innerSpotAngle, 0.1F, 0.0F, 89.0F,
                             "%.1f deg"))
            document_.markDirty();
        if (ImGui::DragFloat("Outer Spot Angle", &light->outerSpotAngle, 0.1F, 0.0F, 90.0F,
                             "%.1f deg"))
            document_.markDirty();
        if (light->innerSpotAngle > light->outerSpotAngle)
            light->innerSpotAngle = light->outerSpotAngle;
    }
    if (ImGui::Checkbox("Cast Shadow", &light->castShadow))
        document_.markDirty();
    if (inputUint("Culling Mask", light->cullingMask))
        document_.markDirty();

    ImGui::Separator();
    if (ImGui::Button("Remove Component")) {
        node.removeComponent<LightComponent>();
        document_.markDirty();
    }

    ImGui::PopID();
}

void InspectorPanel::drawAddComponent(Node& node) {
    if (ImGui::Button("Add Component"))
        ImGui::OpenPopup("AddComponentPopup");
    if (!ImGui::BeginPopup("AddComponentPopup"))
        return;

    if (node.getComponent<MeshComponent>() == nullptr && ImGui::Selectable("Mesh")) {
        node.addComponent<MeshComponent>();
        document_.markDirty();
    }
    if (node.getComponent<MaterialComponent>() == nullptr && ImGui::Selectable("Material")) {
        node.addComponent<MaterialComponent>();
        document_.markDirty();
    }
    if (node.getComponent<CameraComponent>() == nullptr && ImGui::Selectable("Camera")) {
        node.addComponent<CameraComponent>();
        document_.markDirty();
    }
    if (node.getComponent<LightComponent>() == nullptr && ImGui::Selectable("Light")) {
        node.addComponent<LightComponent>();
        document_.markDirty();
    }
    ImGui::EndPopup();
}

std::vector<VirtualPath> InspectorPanel::listAssetFiles(const char* suffix) const {
    std::vector<VirtualPath> result;
    // std::filesystem::path::extension() only yields the final suffix (.json), so asset
    // files are matched through the relative path tail instead.
    for (const VirtualPath& file : FILE_SYSTEM.listFiles(VirtualPath{"assets://"}, true)) {
        if (file.relativePath().ends_with(suffix))
            result.push_back(file);
    }
    return result;
}

} // namespace engine::editor
