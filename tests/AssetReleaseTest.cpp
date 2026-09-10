#include "asset/importer/AssetImportPipeline.h"
#include "asset/importer/FileWatcher.h"
#include "asset/manager/AssetManager.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/scene/RenderScene.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"
#include "TestAssetEnvironment.h"

#include <algorithm>
#include <filesystem>

int main() {
    using namespace engine;
    if (!test::initializeAssetEnvironment(MINI_TEST_COOKED_ASSET_DIR, true)) {
        return 5;
    }
    if (FILE_WATCHER.running() || ASSET_IMPORT_PIPELINE.initialized())
        return 1;

    const auto shader = ASSET_MANAGER.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shaders/vertex_color.shader.json"});
    const auto material = ASSET_MANAGER.loadAsset<MaterialAsset>(
        VirtualPath{"asset://materials/warm_vertex_color.material.json"});
    if (!shader || !material)
        return 2;
    if (ASSET_MANAGER.loadAsset<ShaderAsset>(VirtualPath{"asset://shaders/missing.shader.json"})) {
        return 3;
    }

    const MaterialHandle runtime = MATERIAL_MANAGER.load(material->assetPath());
    if (!runtime || MATERIAL_MANAGER.find(runtime)->shader().name() != "MiniEngine/VertexColor") {
        return 4;
    }

    const auto showcase = ASSET_MANAGER.loadAsset<SceneAsset>(
        VirtualPath{"asset://scenes/blinn_phong_showcase.scene.json"});
    if (!showcase || showcase->nodes.size() != 7)
        return 6;
    const SceneInstantiationContext context{
        .loadMesh = [](const VirtualPath& path) { return MESH_MANAGER.load(path); },
        .loadMaterial = [](const VirtualPath& path) { return MATERIAL_MANAGER.load(path); },
    };
    std::unique_ptr<Scene> scene = showcase->instantiate(context);
    if (!scene)
        return 6;
    RenderScene renderScene;
    scene->buildRenderScene(renderScene, 16.0F / 9.0F);
    if (!renderScene.camera() || renderScene.objects().size() != 4 ||
        !std::ranges::all_of(
            renderScene.objects(),
            [](const RenderObject& object) { return object.materials.size() == 1; }) ||
        renderScene.lights().size() != 2) {
        return 6;
    }
    scene.reset();
    MESH_MANAGER.clear();
    MATERIAL_MANAGER.clear();
    SHADER_MANAGER.clear();
    test::shutdownAssetEnvironment();
}
