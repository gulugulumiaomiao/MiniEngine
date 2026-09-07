#include "asset/derived_data/AssetArtifact.h"
#include "asset/base/AssetMeta.h"
#include "core/serialization/BinaryTransfer.h"
#include "scene/scene/SceneAsset.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/scene/Scene.h"
#include "scene/components/TransformComponent.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <type_traits>
#include <variant>

namespace {

constexpr std::string_view kSceneJson = R"json(
{
  "$schemaVersion": 1,
  "name": "Example Scene",
  "nodes": [
    {
      "id": 1,
      "name": "World",
      "components": [
        {
          "type": "Transform",
          "position": [1.0, 2.0, 3.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        },
        {
          "type": "Mesh",
          "mesh": "../meshes/cube.mesh.json",
          "cast_shadow": false,
          "layer_mask": 3
        },
        {
          "type": "Material",
          "materials": ["asset://materials/default.material.json"]
        }
      ]
    },
    {
      "id": 2,
      "parent": 1,
      "name": "Main Camera",
      "active": false,
      "components": [
        {"type": "Transform", "position": [0.0, 1.0, 5.0]},
        {
          "type": "Camera",
          "projection": "perspective",
          "field_of_view": 75.0,
          "near_plane": 0.2,
          "far_plane": 500.0,
          "primary": true,
          "priority": 10
        }
      ]
    },
    {
      "id": 3,
      "parent": null,
      "name": "Sun",
      "components": [
        {"type": "Transform"},
        {
          "type": "Light",
          "light_type": "directional",
          "color": [1.0, 0.9, 0.8],
          "intensity": 2.0,
          "cast_shadow": true
        }
      ]
    }
  ]
}
)json";

} // namespace

int main() {
    using namespace engine;

    static_assert(std::is_base_of_v<Transferable, TransformComponentAsset>);
    static_assert(std::is_base_of_v<Transferable, MeshComponentAsset>);
    static_assert(std::is_base_of_v<Transferable, MaterialComponentAsset>);
    static_assert(std::is_base_of_v<Transferable, CameraComponentAsset>);
    static_assert(std::is_base_of_v<Transferable, LightComponentAsset>);
    static_assert(std::is_base_of_v<Transferable, SceneNodeAsset>);

    const VirtualPath scenePath{"asset://scenes/example.scene.json"};
    if (inferAssetType(scenePath) != AssetType::Scene ||
        assetTypeFromName("Scene") != AssetType::Scene ||
        std::string_view{assetTypeName(AssetType::Scene)} != "Scene") {
        return 10;
    }
    std::shared_ptr<SceneAsset> asset = detail::parseSceneAsset(scenePath, kSceneJson);
    if (!asset || asset->type() != AssetType::Scene || asset->assetPath() != scenePath ||
        asset->name != "Example Scene" || asset->nodes.size() != 3) {
        return 1;
    }

    const SceneNodeAsset& world = asset->nodes[0];
    if (world.id != 1 || world.parent || world.components.size() != 3) {
        return 2;
    }
    const auto* transform = std::get_if<TransformComponentAsset>(&world.components[0]);
    const auto* mesh = std::get_if<MeshComponentAsset>(&world.components[1]);
    const auto* material = std::get_if<MaterialComponentAsset>(&world.components[2]);
    if (!transform || transform->position != math::Vec3{1.0F, 2.0F, 3.0F} || !mesh ||
        mesh->mesh != VirtualPath{"asset://meshes/cube.mesh.json"} || mesh->castShadow ||
        mesh->layerMask != 3 || !material || material->materials.size() != 1 ||
        material->materials[0] != VirtualPath{"asset://materials/default.material.json"}) {
        return 3;
    }

    const SceneNodeAsset& cameraNode = asset->nodes[1];
    const auto* camera = std::get_if<CameraComponentAsset>(&cameraNode.components[1]);
    if (cameraNode.parent != 1 || cameraNode.active || !camera || camera->fieldOfView != 75.0F ||
        camera->nearPlane != 0.2F || camera->farPlane != 500.0F || !camera->primary ||
        camera->priority != 10) {
        return 4;
    }

    BinaryWriter writer;
    if (!asset->transfer(writer))
        return 5;
    const std::vector<std::byte> binary = writer.takeBytes();
    SceneAsset decoded;
    decoded.setAssetPath(scenePath);
    BinaryReader reader{binary};
    if (binary.empty() || !decoded.transfer(reader) || !reader.finished() ||
        decoded.name != asset->name || decoded.nodes != asset->nodes ||
        decoded.assetPath() != scenePath) {
        return 5;
    }

    std::vector<VirtualPath> loadedMeshes;
    std::vector<VirtualPath> loadedMaterials;
    const SceneInstantiationContext context{
        .loadMesh =
            [&loadedMeshes](const VirtualPath& path) {
                loadedMeshes.push_back(path);
                return MeshHandle{7, 1};
            },
        .loadMaterial =
            [&loadedMaterials](const VirtualPath& path) {
                loadedMaterials.push_back(path);
                return MaterialHandle{9, 1};
            },
    };
    std::unique_ptr<Scene> runtime = decoded.instantiate(context);
    if (!runtime || runtime->name() != "Example Scene" || runtime->nodeCount() != 4 ||
        loadedMeshes.size() != 1 || loadedMaterials.size() != 1 ||
        loadedMeshes.front() != VirtualPath{"asset://meshes/cube.mesh.json"} ||
        loadedMaterials.front() != VirtualPath{"asset://materials/default.material.json"}) {
        return 12;
    }
    const Node* runtimeWorld = runtime->findNode(runtime->root().children().front());
    if (!runtimeWorld || runtimeWorld->name() != "World" ||
        runtimeWorld->transform().localPosition() != math::Vec3{1.0F, 2.0F, 3.0F} ||
        !runtimeWorld->getComponent<MeshComponent>() ||
        runtimeWorld->getComponent<MeshComponent>()->mesh != MeshHandle{7, 1} ||
        runtimeWorld->getComponent<MaterialComponent>()->material(0) != MaterialHandle{9, 1} ||
        runtimeWorld->children().size() != 1) {
        return 13;
    }
    const Node* runtimeCamera = runtime->findNode(runtimeWorld->children().front());
    if (!runtimeCamera || runtimeCamera->name() != "Main Camera" || runtimeCamera->activeSelf()) {
        return 14;
    }

    const AssetArtifact artifact{1, AssetId{1, 2}, AssetType::Scene, scenePath, binary};
    const std::vector<std::byte> artifactBinary = serializeAssetArtifact(artifact);
    const auto decodedArtifact =
        parseAssetArtifact(VirtualPath{"library://artifacts/example.bin"}, artifactBinary);
    if (!decodedArtifact || decodedArtifact->assetType != AssetType::Scene ||
        decodedArtifact->sourcePath != scenePath || decodedArtifact->payload != binary) {
        return 11;
    }

    SceneAsset unchanged = decoded;
    const std::byte invalid[]{std::byte{0}, std::byte{1}};
    BinaryReader invalidReader{invalid};
    if (decoded.transfer(invalidReader) || decoded.name != unchanged.name ||
        decoded.nodes != unchanged.nodes) {
        return 6;
    }

    SceneAsset missingTransform;
    missingTransform.name = "Invalid";
    missingTransform.nodes.push_back(SceneNodeAsset{1, std::nullopt, "Node", true, {}});
    if (validateSceneAsset(missingTransform, scenePath))
        return 7;

    SceneAsset badParent;
    badParent.name = "Invalid";
    badParent.nodes.push_back(SceneNodeAsset{1, 99, "Node", true, {TransformComponentAsset{}}});
    if (validateSceneAsset(badParent, scenePath))
        return 8;

    SceneAsset cycle;
    cycle.name = "Invalid";
    cycle.nodes.push_back(SceneNodeAsset{1, 2, "A", true, {TransformComponentAsset{}}});
    cycle.nodes.push_back(SceneNodeAsset{2, 1, "B", true, {TransformComponentAsset{}}});
    if (validateSceneAsset(cycle, scenePath))
        return 9;

    return 0;
}
