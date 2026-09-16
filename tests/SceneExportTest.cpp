#include "TestAssetEnvironment.h"

#include "asset/database/AssetDatabase.h"
#include "asset/format/SceneAssetFormat.h"
#include "asset/importer/FileWatcher.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/shader/ShaderManager.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"
#include "scene/scene/SceneRuntimeSerializer.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace engine;

[[nodiscard]] MeshHandle registerTriangleMesh() {
    MESH_MANAGER.clear();
    constexpr std::array positions{
        math::Vec3{-1.0F, -1.0F, 0.0F},
        math::Vec3{1.0F, -1.0F, 0.0F},
        math::Vec3{0.0F, 1.0F, 0.0F},
    };
    constexpr std::array<std::uint16_t, 3> indices{0, 1, 2};

    MeshAsset source;
    source.setAssetPath(VirtualPath{"assets://meshes/triangle.mesh.json"});
    source.desc.debugName = "ExportTriangle";
    source.desc.vertexLayout.bindings = {{0, sizeof(math::Vec3), VertexInputRate::Vertex}};
    source.desc.vertexLayout.attributes = {
        {{VertexSemanticType::Position, 0}, VertexFormat::Vec3Float32, 0, 0, 0},
    };
    source.desc.indexType = IndexType::UInt16;
    source.desc.bounds = calculateBounds(positions);
    source.desc.subMeshes.push_back({0, 3, 0, 0, source.desc.bounds});
    if (!source.meshData.setVertexData(0, std::span{positions}) ||
        !source.meshData.setIndexData(std::span{indices}) ||
        !validateMesh(source.desc, source.meshData)) {
        return {};
    }
    return MESH_MANAGER.insertUnkeyed(source.instantiate());
}

[[nodiscard]] std::unique_ptr<Scene> buildExportScene(MeshHandle mesh, MaterialHandle material) {
    auto scene = std::make_unique<Scene>("Export Roundtrip");

    Node* world = scene->findNode(scene->createNode("World"));
    world->transform().setLocalPosition({1.0F, 2.0F, 3.0F});
    world->transform().setLocalScale({2.0F, 2.0F, 2.0F});
    world->addComponent<MeshComponent>()->setAssetMesh(mesh);
    if (MeshComponent* worldMesh = world->getComponent<MeshComponent>()) {
        worldMesh->castShadow = false;
        worldMesh->layerMask = 3;
    }
    world->addComponent<MaterialComponent>()->setMaterial(0, material);

    Node* camera = scene->findNode(scene->createNode("Main Camera"));
    (void)camera->setParent(world->handle());
    camera->setActive(false);
    if (CameraComponent* cameraComponent = camera->addComponent<CameraComponent>()) {
        cameraComponent->fieldOfView = 75.0F;
        cameraComponent->nearPlane = 0.2F;
        cameraComponent->farPlane = 500.0F;
        cameraComponent->primary = true;
        cameraComponent->priority = 10;
    }

    Node* sun = scene->findNode(scene->createNode("Sun"));
    if (LightComponent* light = sun->addComponent<LightComponent>()) {
        light->type = LightType::Spot;
        light->color = {1.0F, 0.9F, 0.8F};
        light->intensity = 2.0F;
        light->range = 12.0F;
        light->innerSpotAngle = 25.0F;
        light->outerSpotAngle = 35.0F;
        light->castShadow = true;
    }

    Node* primitive = scene->findNode(scene->createNode("Primitive Box"));
    primitive->addComponent<MeshComponent>()->setPrimitive(
        BoxGeometry{{1.0F, 2.0F, 3.0F}, 2, 3, 4});

    return scene;
}

} // namespace

int main() {
    using namespace engine;

    if (!test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR))
        return 30;

    const MeshHandle meshHandle = registerTriangleMesh();
    if (!meshHandle)
        return 31;

    MATERIAL_MANAGER.clear();
    MaterialAsset materialAsset;
    materialAsset.setAssetPath(VirtualPath{"assets://materials/test_export.material.json"});
    materialAsset.name = "Export Material";
    const ShaderHandle builtinShader = SHADER_MANAGER.builtinColor();
    if (!builtinShader)
        return 32;
    const MaterialHandle materialHandle =
        MATERIAL_MANAGER.insert(materialAsset.instantiate(builtinShader));
    if (!materialHandle)
        return 33;

    const VirtualPath targetPath{"assets://scenes/export_test.scene.json"};
    std::string error;

    // --- exportSceneToAsset: structure, ordering, parent links and components ---
    std::unique_ptr<Scene> scene = buildExportScene(meshHandle, materialHandle);
    std::unique_ptr<SceneAsset> asset = exportSceneToAsset(*scene, targetPath, error);
    if (!asset)
        return 1;
    if (asset->name != "Export Roundtrip" || asset->assetPath() != targetPath ||
        asset->nodes.size() != 4) {
        return 2;
    }

    const SceneNodeAsset& worldAsset = asset->nodes[0];
    if (worldAsset.id != 1 || worldAsset.parent || worldAsset.name != "World" ||
        !worldAsset.active || worldAsset.components.size() != 3) {
        return 3;
    }
    const auto* worldTransform = std::get_if<TransformComponentAsset>(&worldAsset.components[0]);
    const auto* worldMesh = std::get_if<MeshComponentAsset>(&worldAsset.components[1]);
    const auto* worldMaterial = std::get_if<MaterialComponentAsset>(&worldAsset.components[2]);
    if (!worldTransform || worldTransform->position != math::Vec3{1.0F, 2.0F, 3.0F} ||
        worldTransform->scale != math::Vec3{2.0F} || !worldMesh ||
        worldMesh->sourceType != MeshComponentSourceType::Asset ||
        worldMesh->mesh != VirtualPath{"assets://meshes/triangle.mesh.json"} ||
        worldMesh->castShadow || worldMesh->layerMask != 3 || !worldMaterial ||
        worldMaterial->materials.size() != 1 ||
        worldMaterial->materials[0] !=
            VirtualPath{"assets://materials/test_export.material.json"}) {
        return 4;
    }

    const SceneNodeAsset& cameraAsset = asset->nodes[1];
    const auto* camera = std::get_if<CameraComponentAsset>(&cameraAsset.components[1]);
    if (cameraAsset.id != 2 || cameraAsset.parent != 1 || cameraAsset.name != "Main Camera" ||
        cameraAsset.active || !camera || camera->fieldOfView != 75.0F ||
        camera->nearPlane != 0.2F || camera->farPlane != 500.0F || !camera->primary ||
        camera->priority != 10) {
        return 5;
    }

    const SceneNodeAsset& sunAsset = asset->nodes[2];
    const auto* light = std::get_if<LightComponentAsset>(&sunAsset.components[1]);
    if (sunAsset.id != 3 || sunAsset.parent || sunAsset.name != "Sun" || !light ||
        light->type != LightType::Spot || light->color != math::Vec3{1.0F, 0.9F, 0.8F} ||
        light->intensity != 2.0F || light->range != 12.0F ||
        light->innerSpotAngle != 25.0F || light->outerSpotAngle != 35.0F ||
        !light->castShadow) {
        return 6;
    }

    const SceneNodeAsset& primitiveAsset = asset->nodes[3];
    const auto* primitiveMesh = std::get_if<MeshComponentAsset>(&primitiveAsset.components[1]);
    if (primitiveAsset.id != 4 || primitiveAsset.parent ||
        primitiveAsset.name != "Primitive Box" || !primitiveMesh ||
        primitiveMesh->sourceType != MeshComponentSourceType::Primitive ||
        primitiveMesh->primitiveRecipe.parts.size() != 1) {
        return 7;
    }
    const auto* box =
        std::get_if<BoxGeometry>(&primitiveMesh->primitiveRecipe.parts.front().primitive.value);
    if (!box || box->size != math::Vec3{1.0F, 2.0F, 3.0F} || box->segmentsX != 2 ||
        box->segmentsY != 3 || box->segmentsZ != 4) {
        return 8;
    }

    // --- writeSceneAssetJson + parseSceneAsset roundtrip ---
    const std::string roundtripJson = format::writeSceneAssetJson(*asset, ASSET_DATABASE);
    const std::shared_ptr<SceneAsset> reparsed = format::parseSceneAsset(targetPath, roundtripJson);
    if (!reparsed || reparsed->name != asset->name || reparsed->nodes != asset->nodes)
        return 9;

    // --- instantiate roundtrip back into a runtime Scene ---
    std::vector<VirtualPath> requestedMeshes;
    std::vector<VirtualPath> requestedMaterials;
    const SceneInstantiationContext context{
        .loadMesh =
            [&requestedMeshes, meshHandle](const VirtualPath& path) {
                requestedMeshes.push_back(path);
                return meshHandle;
            },
        .loadMaterial =
            [&requestedMaterials, materialHandle](const VirtualPath& path) {
                requestedMaterials.push_back(path);
                return materialHandle;
            },
    };
    std::unique_ptr<Scene> runtime = reparsed->instantiate(context);
    if (!runtime || runtime->name() != "Export Roundtrip" || runtime->nodeCount() != 5 ||
        requestedMeshes.size() != 1 || requestedMaterials.size() != 1 ||
        requestedMeshes.front() != VirtualPath{"assets://meshes/triangle.mesh.json"} ||
        requestedMaterials.front() !=
            VirtualPath{"assets://materials/test_export.material.json"}) {
        return 10;
    }
    const Node* runtimeWorld = runtime->findNode(runtime->root().children().front());
    if (!runtimeWorld || runtimeWorld->name() != "World" ||
        runtimeWorld->transform().localPosition() != math::Vec3{1.0F, 2.0F, 3.0F} ||
        runtimeWorld->transform().localScale() != math::Vec3{2.0F} ||
        !runtimeWorld->getComponent<MeshComponent>() ||
        runtimeWorld->getComponent<MeshComponent>()->mesh() != meshHandle ||
        runtimeWorld->getComponent<MeshComponent>()->castShadow ||
        runtimeWorld->getComponent<MeshComponent>()->layerMask != 3 ||
        runtimeWorld->getComponent<MaterialComponent>()->material(0) != materialHandle ||
        runtimeWorld->children().size() != 1) {
        return 11;
    }
    const Node* runtimeCamera = runtime->findNode(runtimeWorld->children().front());
    if (!runtimeCamera || runtimeCamera->name() != "Main Camera" ||
        runtimeCamera->activeSelf() || !runtimeCamera->getComponent<CameraComponent>() ||
        runtimeCamera->getComponent<CameraComponent>()->fieldOfView != 75.0F) {
        return 12;
    }

    // --- failure: asset Mesh component whose handle is not registered ---
    {
        Scene broken;
        broken.findNode(broken.createNode("Broken"))
            ->addComponent<MeshComponent>()
            ->setAssetMesh(MeshHandle{999, 1});
        error.clear();
        if (exportSceneToAsset(broken, targetPath, error) ||
            error.find("Broken") == std::string::npos) {
            return 13;
        }
    }

    // --- failure: primitive part with a non-identity transform ---
    {
        Scene broken;
        MeshComponent* component =
            broken.findNode(broken.createNode("Skewed"))->addComponent<MeshComponent>();
        component->setPrimitive(BoxGeometry{});
        if (MeshBuildRecipe* recipe = component->editPrimitiveRecipe())
            recipe->parts.front().translation = {1.0F, 0.0F, 0.0F};
        error.clear();
        if (exportSceneToAsset(broken, targetPath, error) ||
            error.find("Skewed") == std::string::npos) {
            return 14;
        }
    }

    // --- failure: Material component whose handle is not registered ---
    {
        Scene broken;
        broken.findNode(broken.createNode("Unshaded"))
            ->addComponent<MaterialComponent>()
            ->setMaterial(0, MaterialHandle{999, 1});
        error.clear();
        if (exportSceneToAsset(broken, targetPath, error) ||
            error.find("Unshaded") == std::string::npos) {
            return 15;
        }
    }

    // --- writeSceneAssetJson + writeTextAtomic: roundtrip ---
    FILE_WATCHER.stop();
    ASSET_MANAGER.shutdown();
    (void)FILE_SYSTEM.unmount("assets");
    (void)FILE_SYSTEM.unmount("library");

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "MiniEngineSceneExport";
    std::error_code filesystemError;
    std::filesystem::remove_all(root, filesystemError);
    std::filesystem::create_directories(root / "scenes", filesystemError);
    if (!FILE_SYSTEM.mountDirectory("assets", root, false))
        return 17;

    const std::string json = format::writeSceneAssetJson(*asset, ASSET_DATABASE);
    if (json.empty())
        return 18;
    if (!FILE_SYSTEM.writeTextAtomic(targetPath, json))
        return 18;
    const std::optional<std::string> written = FILE_SYSTEM.readText(targetPath);
    if (!written)
        return 19;
    const std::shared_ptr<SceneAsset> fromDisk = format::parseSceneAsset(targetPath, *written);
    if (!fromDisk || fromDisk->name != asset->name || fromDisk->nodes != asset->nodes)
        return 20;
    (void)FILE_SYSTEM.unmount("assets");

    runtime.reset();
    scene.reset();
    MESH_MANAGER.clear();
    MATERIAL_MANAGER.clear();
    return 0;
}
