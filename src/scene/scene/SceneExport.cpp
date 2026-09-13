#include "scene/scene/SceneExport.h"

#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine {
namespace {

using Json = nlohmann::ordered_json;

bool isIdentityPartTransform(const MeshPrimitivePart& part) {
    return part.translation == math::Vec3{0.0F} &&
           part.rotation == math::Quat{1.0F, 0.0F, 0.0F, 0.0F} &&
           part.scale == math::Vec3{1.0F} && part.materialSlot == 0;
}

[[nodiscard]] bool exportMeshComponent(const MeshComponent& component,
                                       MeshComponentAsset& result,
                                       std::string& error,
                                       std::string_view nodeName) {
    result.visible = component.visible;
    result.castShadow = component.castShadow;
    result.receiveShadow = component.receiveShadow;
    result.layerMask = component.layerMask;
    result.enabled = component.enabled();

    if (component.sourceType() == MeshComponentSourceType::Asset) {
        const Mesh* mesh = component.mesh() ? MESH_MANAGER.find(component.mesh()) : nullptr;
        if (!mesh || !mesh->assetPath().valid() || mesh->assetPath().scheme() != "assets" ||
            !mesh->assetPath().relativePath().ends_with(".mesh.json")) {
            error = "node '" + std::string{nodeName} +
                    "' has a Mesh component without a resolvable asset mesh";
            return false;
        }
        result.sourceType = MeshComponentSourceType::Asset;
        result.mesh = mesh->assetPath();
        return true;
    }

    const MeshBuildRecipe* recipe = component.primitiveRecipe();
    if (!recipe || recipe->parts.size() != 1 || !isIdentityPartTransform(recipe->parts.front())) {
        error = "node '" + std::string{nodeName} +
                "' has a primitive Mesh component that cannot be represented in scene JSON";
        return false;
    }
    result.sourceType = MeshComponentSourceType::Primitive;
    result.primitiveRecipe.name = "Scene Runtime Primitive";
    result.primitiveRecipe.parts.push_back(recipe->parts.front().primitive);
    result.primitiveRecipe.usage = MeshUsage::Dynamic;
    result.primitiveRecipe.keepCpuCopy = true;
    return true;
}

[[nodiscard]] bool exportMaterialComponent(const MaterialComponent& component,
                                           MaterialComponentAsset& result,
                                           std::string& error,
                                           std::string_view nodeName) {
    result.enabled = component.enabled();
    for (MaterialHandle handle : component.materials()) {
        const Material* material = handle ? MATERIAL_MANAGER.find(handle) : nullptr;
        if (!material || !material->assetPath().valid() ||
            material->assetPath().scheme() != "assets" ||
            !material->assetPath().relativePath().ends_with(".material.json")) {
            error = "node '" + std::string{nodeName} +
                    "' has a Material component without a resolvable asset material";
            return false;
        }
        result.materials.push_back(material->assetPath());
    }
    return true;
}

[[nodiscard]] bool exportNode(const Scene& scene,
                              NodeHandle handle,
                              std::optional<SceneNodeAssetId> parentId,
                              SceneNodeAssetId& nextId,
                              SceneAsset& asset,
                              std::string& error) {
    const Node* node = scene.findNode(handle);
    if (!node) {
        error = "scene contains an invalid node handle";
        return false;
    }

    SceneNodeAsset nodeAsset;
    nodeAsset.id = nextId++;
    nodeAsset.parent = parentId;
    nodeAsset.name = std::string{node->name()};
    nodeAsset.active = node->activeSelf();

    TransformComponentAsset transform;
    transform.position = node->transform().localPosition();
    transform.rotation = node->transform().localRotation();
    transform.scale = node->transform().localScale();
    nodeAsset.components.emplace_back(std::move(transform));

    if (const MeshComponent* mesh = node->getComponent<MeshComponent>()) {
        MeshComponentAsset value;
        if (!exportMeshComponent(*mesh, value, error, node->name()))
            return false;
        nodeAsset.components.emplace_back(std::move(value));
    }
    if (const MaterialComponent* material = node->getComponent<MaterialComponent>()) {
        MaterialComponentAsset value;
        if (!exportMaterialComponent(*material, value, error, node->name()))
            return false;
        nodeAsset.components.emplace_back(std::move(value));
    }
    if (const CameraComponent* camera = node->getComponent<CameraComponent>()) {
        CameraComponentAsset value;
        value.projection = camera->projection;
        value.fieldOfView = camera->fieldOfView;
        value.orthographicSize = camera->orthographicSize;
        value.nearPlane = camera->nearPlane;
        value.farPlane = camera->farPlane;
        value.clearColor = camera->clearColor;
        value.cullingMask = camera->cullingMask;
        value.priority = camera->priority;
        value.primary = camera->primary;
        value.enabled = camera->enabled();
        nodeAsset.components.emplace_back(std::move(value));
    }
    if (const LightComponent* light = node->getComponent<LightComponent>()) {
        LightComponentAsset value;
        value.type = light->type;
        value.color = light->color;
        value.intensity = light->intensity;
        value.range = light->range;
        value.innerSpotAngle = light->innerSpotAngle;
        value.outerSpotAngle = light->outerSpotAngle;
        value.castShadow = light->castShadow;
        value.cullingMask = light->cullingMask;
        value.enabled = light->enabled();
        nodeAsset.components.emplace_back(std::move(value));
    }

    asset.nodes.push_back(std::move(nodeAsset));
    const SceneNodeAssetId ownId = asset.nodes.back().id;
    for (NodeHandle child : node->children()) {
        if (!exportNode(scene, child, ownId, nextId, asset, error))
            return false;
    }
    return true;
}

void writeVec3(Json& target, const char* key, const math::Vec3& value) {
    target[key] = Json::array({value.x, value.y, value.z});
}

void writePrimitiveParameters(Json& primitive, const MeshPrimitivePart& part) {
    std::visit(
        [&](const auto& geometry) {
            using T = std::decay_t<decltype(geometry)>;
            Json parameters;
            if constexpr (std::is_same_v<T, PlaneGeometry>) {
                primitive["type"] = "plane";
                parameters["size"] = Json::array({geometry.size.x, geometry.size.y});
                parameters["segments_x"] = geometry.segmentsX;
                parameters["segments_z"] = geometry.segmentsZ;
            } else if constexpr (std::is_same_v<T, BoxGeometry>) {
                primitive["type"] = "box";
                writeVec3(parameters, "size", geometry.size);
                parameters["segments_x"] = geometry.segmentsX;
                parameters["segments_y"] = geometry.segmentsY;
                parameters["segments_z"] = geometry.segmentsZ;
            } else if constexpr (std::is_same_v<T, UvSphereGeometry>) {
                primitive["type"] = "sphere";
                parameters["radius"] = geometry.radius;
                parameters["longitude_segments"] = geometry.longitudeSegments;
                parameters["latitude_segments"] = geometry.latitudeSegments;
            } else {
                primitive["type"] = "cylinder";
                parameters["bottom_radius"] = geometry.bottomRadius;
                parameters["top_radius"] = geometry.topRadius;
                parameters["height"] = geometry.height;
                parameters["radial_segments"] = geometry.radialSegments;
                parameters["height_segments"] = geometry.heightSegments;
                parameters["cap_bottom"] = geometry.capBottom;
                parameters["cap_top"] = geometry.capTop;
            }
            primitive["parameters"] = std::move(parameters);
        },
        part.primitive.value);
}

} // namespace

std::unique_ptr<SceneAsset> exportSceneToAsset(const Scene& scene,
                                               const VirtualPath& targetPath,
                                               std::string& error) {
    auto asset = std::make_unique<SceneAsset>();
    asset->name = std::string{scene.name()};
    asset->setAssetPath(targetPath);

    SceneNodeAssetId nextId = 1;
    for (NodeHandle child : scene.root().children()) {
        if (!exportNode(scene, child, std::nullopt, nextId, *asset, error))
            return nullptr;
    }

    if (!validateSceneAsset(*asset, targetPath)) {
        error = error.empty() ? "exported Scene failed validation" : error;
        return nullptr;
    }
    return asset;
}

std::string writeSceneAssetJson(const SceneAsset& asset) {
    Json root;
    root["$schemaVersion"] = 1;
    root["name"] = asset.name;
    Json nodes = Json::array();
    for (const SceneNodeAsset& node : asset.nodes) {
        Json serialized;
        serialized["id"] = node.id;
        if (node.parent)
            serialized["parent"] = *node.parent;
        serialized["name"] = node.name;
        serialized["active"] = node.active;
        Json components = Json::array();
        for (const SceneComponentAsset& component : node.components) {
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    Json serializedComponent;
                    if constexpr (std::is_same_v<T, TransformComponentAsset>) {
                        serializedComponent["type"] = "Transform";
                        writeVec3(serializedComponent, "position", value.position);
                        serializedComponent["rotation"] = Json::array(
                            {value.rotation.x, value.rotation.y, value.rotation.z,
                             value.rotation.w});
                        writeVec3(serializedComponent, "scale", value.scale);
                    } else if constexpr (std::is_same_v<T, MeshComponentAsset>) {
                        serializedComponent["type"] = "Mesh";
                        if (value.sourceType == MeshComponentSourceType::Asset) {
                            serializedComponent["mesh"] = value.mesh.string();
                        } else {
                            Json primitive;
                            writePrimitiveParameters(primitive, value.primitiveRecipe.parts.front());
                            serializedComponent["primitive"] = std::move(primitive);
                        }
                        serializedComponent["enabled"] = value.enabled;
                        serializedComponent["visible"] = value.visible;
                        serializedComponent["cast_shadow"] = value.castShadow;
                        serializedComponent["receive_shadow"] = value.receiveShadow;
                        serializedComponent["layer_mask"] = value.layerMask;
                    } else if constexpr (std::is_same_v<T, MaterialComponentAsset>) {
                        serializedComponent["type"] = "Material";
                        Json materials = Json::array();
                        for (const VirtualPath& material : value.materials)
                            materials.push_back(material.string());
                        serializedComponent["materials"] = std::move(materials);
                        serializedComponent["enabled"] = value.enabled;
                    } else if constexpr (std::is_same_v<T, CameraComponentAsset>) {
                        serializedComponent["type"] = "Camera";
                        serializedComponent["projection"] =
                            value.projection == CameraProjection::Perspective ? "perspective"
                                                                              : "orthographic";
                        serializedComponent["field_of_view"] = value.fieldOfView;
                        serializedComponent["orthographic_size"] = value.orthographicSize;
                        serializedComponent["near_plane"] = value.nearPlane;
                        serializedComponent["far_plane"] = value.farPlane;
                        serializedComponent["clear_color"] = Json::array(
                            {value.clearColor.x, value.clearColor.y, value.clearColor.z,
                             value.clearColor.w});
                        serializedComponent["culling_mask"] = value.cullingMask;
                        serializedComponent["priority"] = value.priority;
                        serializedComponent["primary"] = value.primary;
                        serializedComponent["enabled"] = value.enabled;
                    } else {
                        serializedComponent["type"] = "Light";
                        switch (value.type) {
                        case LightType::Directional:
                            serializedComponent["light_type"] = "directional";
                            break;
                        case LightType::Point:
                            serializedComponent["light_type"] = "point";
                            break;
                        case LightType::Spot:
                            serializedComponent["light_type"] = "spot";
                            break;
                        }
                        writeVec3(serializedComponent, "color", value.color);
                        serializedComponent["intensity"] = value.intensity;
                        serializedComponent["range"] = value.range;
                        serializedComponent["inner_spot_angle"] = value.innerSpotAngle;
                        serializedComponent["outer_spot_angle"] = value.outerSpotAngle;
                        serializedComponent["cast_shadow"] = value.castShadow;
                        serializedComponent["culling_mask"] = value.cullingMask;
                        serializedComponent["enabled"] = value.enabled;
                    }
                    components.push_back(std::move(serializedComponent));
                },
                component);
        }
        serialized["components"] = std::move(components);
        nodes.push_back(std::move(serialized));
    }
    root["nodes"] = std::move(nodes);
    return root.dump(2) + "\n";
}

bool saveSceneToFile(const Scene& scene, const VirtualPath& targetPath, std::string& error) {
    if (!targetPath.valid() || targetPath.scheme() != "assets" ||
        !targetPath.relativePath().ends_with(".scene.json")) {
        error = "invalid Scene target path: " +
                (targetPath.valid() ? targetPath.string() : std::string{"<invalid>"});
        return false;
    }

    const std::unique_ptr<SceneAsset> asset = exportSceneToAsset(scene, targetPath, error);
    if (!asset)
        return false;

    const std::string json = writeSceneAssetJson(*asset);
    if (!FILE_SYSTEM.writeTextAtomic(targetPath, json)) {
        error = "cannot write " + targetPath.string();
        return false;
    }
    return true;
}

} // namespace engine
