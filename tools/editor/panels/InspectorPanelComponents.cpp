#include "tools/editor/panels/InspectorPanel.h"

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
#include "tools/editor/model/SceneDocument.h"
#include "tools/editor/widgets/EditorWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace engine::editor {
namespace {

// One multi-selection field of drawMultiTransform: take the first component's
// value, check the whole group for equality, then either show the shared value
// (an edit writes back to every component) or a grey placeholder for mixed
// values. Returns whether an edit happened, so the caller can markDirty once.
template <typename Value>
bool multiField(const char* label,
                const std::vector<TransformComponent*>& transforms,
                const Value& (TransformComponent::*get)() const,
                bool (*drag)(const char*, Value&),
                void (TransformComponent::*set)(const Value&)) {
    Value value = (transforms.front()->*get)();
    const bool uniform = std::ranges::all_of(transforms, [&](const TransformComponent* transform) {
        return (transform->*get)() == value;
    });
    if (!uniform) {
        ImGui::TextDisabled("%s  -", label);
        return false;
    }
    if (!drag(label, value))
        return false;
    for (TransformComponent* transform : transforms)
        (transform->*set)(value);
    return true;
}

} // namespace

template <typename T>
bool InspectorPanel::beginComponent(const char* title, T* component) {
    if (component == nullptr)
        return false;
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
        return false;
    ImGui::PushID(title);

    bool enabled = component->enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        component->setEnabled(enabled);
        document_.markDirty();
    }
    return true;
}

template <typename T>
void InspectorPanel::endComponent(Node& node) {
    ImGui::Separator();
    if (ImGui::Button("Remove Component")) {
        node.removeComponent<T>();
        document_.markDirty();
    }
    ImGui::PopID();
}

// The shell templates live with their only users and are instantiated here for
// the four optional components; InspectorPanel.h stays free of component includes.
template bool InspectorPanel::beginComponent<MeshComponent>(const char*, MeshComponent*);
template bool InspectorPanel::beginComponent<MaterialComponent>(const char*, MaterialComponent*);
template bool InspectorPanel::beginComponent<CameraComponent>(const char*, CameraComponent*);
template bool InspectorPanel::beginComponent<LightComponent>(const char*, LightComponent*);
template void InspectorPanel::endComponent<MeshComponent>(Node&);
template void InspectorPanel::endComponent<MaterialComponent>(Node&);
template void InspectorPanel::endComponent<CameraComponent>(Node&);
template void InspectorPanel::endComponent<LightComponent>(Node&);

void InspectorPanel::drawMultiHeader(const SelectionSet<RID>& selection) {
    ImGui::Text("%u nodes selected", static_cast<unsigned>(selection.size()));
    ImGui::Separator();
}

void InspectorPanel::drawMultiActive(const SelectionSet<RID>& selection) {
    const std::vector<Node*> nodes = collectNodes(selection);
    if (nodes.empty())
        return;
    bool anyActive = false, anyInactive = false;
    for (const Node* node : nodes)
        node->activeSelf() ? anyActive = true : anyInactive = true;
    bool active = false;
    if (mixedCheckbox("Active", anyActive, anyInactive, active)) {
        for (Node* node : nodes)
            node->setActive(active);
        document_.markDirty();
    }
    ImGui::Separator();
}

void InspectorPanel::drawMultiTransform(const SelectionSet<RID>& selection) {
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
    if (multiField("Position", transforms, &TransformComponent::localPosition, dragVec3,
                   &TransformComponent::setLocalPosition))
        document_.markDirty();
    if (multiField("Rotation", transforms, &TransformComponent::localRotation, dragEuler,
                   &TransformComponent::setLocalRotation))
        document_.markDirty();
    if (multiField("Scale", transforms, &TransformComponent::localScale, dragVec3,
                   &TransformComponent::setLocalScale))
        document_.markDirty();
    ImGui::PopID();
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
    if (!beginComponent("Mesh", mesh))
        return;

    if (ImGui::Checkbox("Visible", &mesh->visible))
        document_.markDirty();
    if (ImGui::Checkbox("Cast Shadow", &mesh->castShadow))
        document_.markDirty();
    if (ImGui::Checkbox("Receive Shadow", &mesh->receiveShadow))
        document_.markDirty();
    if (inputUint("Layer Mask", mesh->layerMask))
        document_.markDirty();

    if (mesh->sourceType() == MeshComponentSourceType::Asset) {
        const std::vector<VirtualPath> meshes = collectAssets(AssetType::Mesh);
        const Mesh* current = mesh->mesh().get();
        const VirtualPath currentPath = current != nullptr ? current->assetPath() : VirtualPath{};
        VirtualPath chosen;
        if (assetCombo("Mesh Asset", meshes, currentPath, chosen)) {
            const Ref<Mesh> loaded = MESH_RESOURCE_MANAGER.load(chosen);
            if (loaded) {
                mesh->setAssetMesh(loaded);
                document_.markDirty();
            } else {
                statusMessage_ = "Failed to load mesh: " + chosen.string();
            }
        }
    } else {
        drawPrimitive(*mesh);
    }

    endComponent<MeshComponent>(node);
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
    if (!beginComponent("Material", material))
        return;

    const std::vector<VirtualPath> materials = collectAssets(AssetType::Material);
    const std::vector<Ref<Material>> slots = material->materials();
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        ImGui::PushID(static_cast<int>(slot));
        const Material* current = slots[slot].get();
        const VirtualPath currentPath = current != nullptr ? current->assetPath() : VirtualPath{};
        const std::string label = "Slot " + std::to_string(slot);
        VirtualPath chosen;
        if (assetCombo(label.c_str(), materials, currentPath, chosen)) {
            const Ref<Material> loaded = MATERIAL_RESOURCE_MANAGER.load(chosen);
            if (loaded) {
                material->setMaterial(static_cast<std::uint32_t>(slot), loaded);
                document_.markDirty();
            } else {
                statusMessage_ = "Failed to load material: " + chosen.string();
            }
        }
        // Embedding the shared widget here is the reuse path: slot materials
        // are load(path) instances, i.e. the asset itself, so component edits
        // apply to every user of the material and write back to the asset —
        // the same semantics as the Project asset view.
        if (slots[slot])
            materialInspector_.draw(slots[slot]);
        ImGui::PopID();
    }

    if (slots.empty())
        ImGui::TextUnformatted("No material slots");
    if (ImGui::Button("Clear Slots")) {
        material->clearMaterials();
        document_.markDirty();
    }

    endComponent<MaterialComponent>(node);
}

void InspectorPanel::drawCamera(Node& node) {
    CameraComponent* camera = node.getComponent<CameraComponent>();
    if (!beginComponent("Camera", camera))
        return;

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

    endComponent<CameraComponent>(node);
}

void InspectorPanel::drawLight(Node& node) {
    LightComponent* light = node.getComponent<LightComponent>();
    if (!beginComponent("Light", light))
        return;

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

    endComponent<LightComponent>(node);
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

} // namespace engine::editor
