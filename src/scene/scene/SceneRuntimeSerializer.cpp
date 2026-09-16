#include "scene/scene/SceneRuntimeSerializer.h"

#include "asset/format/SceneAssetFormat.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/components/TransformComponent.h"
#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine {
namespace {

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

    if (!format::validateSceneAsset(*asset, targetPath)) {
        error = error.empty() ? "exported Scene failed validation" : error;
        return nullptr;
    }
    return asset;
}

} // namespace engine
