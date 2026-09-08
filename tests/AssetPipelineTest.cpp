#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "asset/base/AssetMeta.h"
#include "asset/importer/FileWatcher.h"
#include "core/filesystem/FileSystem.h"
#include "asset/manager/AssetManager.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"
#include "TestAssetEnvironment.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace std::chrono_literals;

static_assert(std::is_base_of_v<engine::Singleton<engine::FileSystem>, engine::FileSystem>);
static_assert(std::is_base_of_v<engine::Singleton<engine::AssetDatabase>, engine::AssetDatabase>);
static_assert(
    std::is_base_of_v<engine::Singleton<engine::AssetImportPipeline>, engine::AssetImportPipeline>);
static_assert(std::is_base_of_v<engine::Singleton<engine::FileWatcher>, engine::FileWatcher>);
static_assert(std::is_base_of_v<engine::Singleton<engine::AssetManager>, engine::AssetManager>);
static_assert(std::is_base_of_v<engine::Singleton<engine::ShaderManager>, engine::ShaderManager>);
static_assert(
    std::is_base_of_v<engine::Singleton<engine::MaterialManager>, engine::MaterialManager>);
static_assert(std::is_base_of_v<engine::Singleton<engine::MeshManager>, engine::MeshManager>);
static_assert(std::is_base_of_v<engine::InstanceManager<engine::Shader, engine::ShaderHandle>,
                                engine::ShaderManager>);
static_assert(std::is_base_of_v<engine::InstanceManager<engine::Material, engine::MaterialHandle>,
                                engine::MaterialManager>);
static_assert(std::is_base_of_v<engine::InstanceManager<engine::Mesh, engine::MeshHandle>,
                                engine::MeshManager>);
static_assert(std::is_abstract_v<engine::InstanceManager<engine::Shader, engine::ShaderHandle>>);

struct TestWorkspace {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / "MiniEngineAssetPipelineTest";
    std::filesystem::path assets = root / "assets";

    TestWorkspace() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(assets / "shaders", error);
        std::filesystem::create_directories(assets / "materials", error);
        std::filesystem::create_directories(assets / "meshes", error);
        std::filesystem::create_directories(assets / "scenes", error);
    }

    ~TestWorkspace() {
        MESH_MANAGER.clear();
        MATERIAL_MANAGER.clear();
        SHADER_MANAGER.clear();
        engine::test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

std::string shaderSource(std::string_view name, bool extraProperty = false) {
    std::string properties = R"json(
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] })json";
    if (extraProperty) {
        properties += R"json(,
    { "name": "Extra", "type": "Vec2", "default": [0.5, 0.5] })json";
    }
    return std::string{R"json({
  "$schemaVersion": 1,
  "name": ")json"} +
           std::string{name} + R"json(",
  "properties": [)json" +
           properties + R"json(],
  "subShader": {
    "passes": [{
      "name": "Forward",
      "lightMode": "Forward",
      "program": { "vertex": "simple.vert", "frag": "simple.frag" }
    }]
  }
})json";
}

bool waitForEvents() {
    std::this_thread::sleep_for(120ms);
    return true;
}

std::string meshSource(float leftX) {
    const std::array positions{
        engine::math::Vec3{leftX, -1.0F, 0.0F},
        engine::math::Vec3{1.0F, -1.0F, 0.0F},
        engine::math::Vec3{0.0F, 1.0F, 0.0F},
    };
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : std::as_bytes(std::span{positions})) {
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    }
    nlohmann::json root{
        {"name", "Pipeline Mesh"},
        {"index_type", "uint16"},
        {"usage", "dynamic"},
        {"bindings", {{{"binding", 0}, {"stride", sizeof(engine::math::Vec3)}}}},
        {"attributes",
         {{{"semantic", "position"},
           {"format", "vec3_float32"},
           {"location", 0},
           {"binding", 0},
           {"offset", 0}}}},
        {"vertex_streams", {{{"binding", 0}, {"vertex_count", 3}, {"bytes", bytes}}}},
        {"indices", {0, 1, 2}},
    };
    return root.dump();
}

} // namespace

int main() {
    using namespace engine;

    TestWorkspace workspace;
    if (!test::initializeAssetEnvironment(workspace.assets))
        return 20;
    FILE_WATCHER.stop();

    if (!FILE_SYSTEM.writeText(VirtualPath{"asset://shaders/simple.vert"},
                               "#version 450\nvoid main(){gl_Position=vec4(0);}\n") ||
        !FILE_SYSTEM.writeText(VirtualPath{"asset://shaders/simple.frag"},
                               "#version 450\nlayout(location=0) out vec4 c;"
                               "void main(){c=vec4(1);}\n") ||
        !FILE_SYSTEM.writeText(VirtualPath{"asset://shaders/first.shader.json"},
                               shaderSource("Tests/First")) ||
        !FILE_SYSTEM.writeText(VirtualPath{"asset://shaders/second.shader.json"},
                               shaderSource("Tests/Second", true)) ||
        !FILE_SYSTEM.writeText(VirtualPath{"asset://materials/test.material.json"},
                               R"json({
  "$schemaVersion": 1,
  "name": "Pipeline Material",
  "shader": "shaders/first.shader.json",
  "properties": { "BaseColor": [0.25, 0.5, 0.75, 1.0] },
  "keywords": [],
  "renderQueue": 2450
})json")) {
        return 1;
    }
    if (!ASSET_IMPORT_PIPELINE.scanAll())
        return 2;

    const VirtualPath meshPath{"asset://meshes/test.mesh.json"};
    if (!FILE_SYSTEM.writeText(meshPath, meshSource(-1.0F)))
        return 25;
    const MeshHandle meshHandle = MESH_MANAGER.load(meshPath);
    const auto meshAsset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    Mesh* runtimeMesh = MESH_MANAGER.find(meshHandle);
    if (!meshHandle || !meshAsset || !runtimeMesh || runtimeMesh->assetPath() != meshPath ||
        runtimeMesh->data().indexCount != 3) {
        return 26;
    }
    const std::uint64_t meshVersion = runtimeMesh->version();
    if (!FILE_SYSTEM.writeText(meshPath, meshSource(-2.0F)) ||
        !ASSET_IMPORT_PIPELINE.reimportAsset(meshPath)) {
        return 27;
    }
    runtimeMesh = MESH_MANAGER.find(meshHandle);
    if (!runtimeMesh || runtimeMesh->version() != meshVersion + 1 || !runtimeMesh->dirty()) {
        return 28;
    }

    const VirtualPath scenePath{"asset://scenes/test.scene.json"};
    const std::string sceneSource = R"json({
  "$schemaVersion": 1,
  "name": "Pipeline Scene",
  "nodes": [{
    "id": 1,
    "name": "Triangle",
    "components": [
      {"type": "Transform"},
      {"type": "Mesh", "mesh": "../meshes/test.mesh.json"},
      {"type": "Material", "materials": ["../materials/test.material.json"]}
    ]
  }]
})json";
    int sceneChanges = 0;
    ASSET_MANAGER.setChangeListener(
        [&sceneChanges, &scenePath](const VirtualPath& path, AssetType type, bool removed) {
            if (path == scenePath && type == AssetType::Scene && !removed) {
                ++sceneChanges;
            }
        });
    if (!FILE_SYSTEM.writeText(scenePath, sceneSource) ||
        !ASSET_IMPORT_PIPELINE.importAsset(scenePath)) {
        return 29;
    }
    const auto sceneAsset = ASSET_MANAGER.loadAsset<SceneAsset>(scenePath);
    const auto sceneRecord = ASSET_DATABASE.findByPath(scenePath);
    if (!sceneAsset || !sceneRecord || sceneAsset->name != "Pipeline Scene" ||
        sceneRecord->dependencies !=
            std::vector<VirtualPath>{VirtualPath{"asset://materials/test.material.json"},
                                     meshPath} ||
        sceneChanges != 1) {
        return 30;
    }
    const SceneInstantiationContext sceneContext{
        .loadMesh = [](const VirtualPath& path) { return MESH_MANAGER.load(path); },
        .loadMaterial = [](const VirtualPath& path) { return MATERIAL_MANAGER.load(path); },
    };
    const std::unique_ptr<Scene> runtimeScene = sceneAsset->instantiate(sceneContext);
    if (!runtimeScene || runtimeScene->nodeCount() != 2)
        return 31;

    const VirtualPath firstPath{"asset://shaders/first.shader.json"};
    const VirtualPath materialPath{"asset://materials/test.material.json"};
    const auto firstRecord = ASSET_DATABASE.findByPath(firstPath);
    const auto materialRecord = ASSET_DATABASE.findByPath(materialPath);
    if (!firstRecord || !materialRecord || firstRecord->status != AssetImportStatus::Imported ||
        materialRecord->status != AssetImportStatus::Imported ||
        materialRecord->dependencies != std::vector<VirtualPath>{firstPath} ||
        !FILE_SYSTEM.isFile(firstRecord->metaPath) ||
        !FILE_SYSTEM.isFile(firstRecord->artifactPath) ||
        ASSET_DATABASE.dependentsOf(firstPath) != std::vector<VirtualPath>{materialPath}) {
        return 3;
    }

    const VirtualPath latePath{"asset://shaders/late.shader.json"};
    if (!FILE_SYSTEM.writeText(latePath, shaderSource("Tests/Late")))
        return 18;
    const auto lateAsset = ASSET_MANAGER.loadAsset<ShaderAsset>(latePath);
    const auto lateRecord = ASSET_DATABASE.findByPath(latePath);
    if (!lateAsset || !lateRecord || lateRecord->status != AssetImportStatus::Imported ||
        !FILE_SYSTEM.isFile(lateRecord->artifactPath)) {
        return 19;
    }

    const auto shaderAssetA = ASSET_MANAGER.loadAsset<ShaderAsset>(firstPath);
    const auto shaderAssetB = ASSET_MANAGER.loadAsset<ShaderAsset>(firstPath);
    const auto materialAsset = ASSET_MANAGER.loadAsset<MaterialAsset>(materialPath);
    if (!shaderAssetA || shaderAssetA != shaderAssetB || !materialAsset ||
        materialAsset->shader != firstPath || materialAsset->renderQueue != 2450) {
        return 4;
    }

    const ShaderHandle firstHandleA = SHADER_MANAGER.load(firstPath);
    const ShaderHandle firstHandleB = SHADER_MANAGER.load(firstPath);
    const MaterialHandle materialHandle = MATERIAL_MANAGER.load(materialPath);
    if (!firstHandleA || firstHandleA != firstHandleB || !materialHandle)
        return 5;
    Material& material = *MATERIAL_MANAGER.find(materialHandle);
    if (material.shaderHandle() != firstHandleA || material.renderQueue != 2450 ||
        material.getVec4("BaseColor") != math::Vec4{0.25F, 0.5F, 0.75F, 1.0F}) {
        return 6;
    }

    MATERIAL_MANAGER.setShader(materialHandle, VirtualPath{"asset://shaders/second.shader.json"});
    const ShaderHandle secondHandle = material.shaderHandle();
    if (!secondHandle || secondHandle == firstHandleA ||
        material.shader().name() != "Tests/Second" ||
        material.getVec4("BaseColor") != math::Vec4{0.25F, 0.5F, 0.75F, 1.0F} ||
        material.getVec2("Extra") != math::Vec2{0.5F, 0.5F}) {
        return 7;
    }

    if (!FILE_WATCHER.start(VirtualPath{"asset://"}, 100ms, false))
        return 8;
    const std::uint64_t oldRevision = SHADER_MANAGER.find(firstHandleA)->revision();
    if (!FILE_SYSTEM.writeText(firstPath, shaderSource("Tests/First Reloaded"))) {
        return 9;
    }
    FILE_WATCHER.scanNow();
    waitForEvents();
    ASSET_IMPORT_PIPELINE.processFileEvents();
    if (SHADER_MANAGER.find(firstPath) != SHADER_MANAGER.find(firstHandleA) ||
        SHADER_MANAGER.find(firstHandleA)->revision() != oldRevision + 1 ||
        SHADER_MANAGER.find(firstHandleA)->name() != "Tests/First Reloaded") {
        return 10;
    }

    const std::uint64_t validRevision = SHADER_MANAGER.find(firstHandleA)->revision();
    if (!FILE_SYSTEM.writeText(firstPath, "{ invalid json"))
        return 11;
    FILE_WATCHER.scanNow();
    waitForEvents();
    ASSET_IMPORT_PIPELINE.processFileEvents();
    const auto failedRecord = ASSET_DATABASE.findByPath(firstPath);
    if (!failedRecord || failedRecord->status != AssetImportStatus::Failed ||
        SHADER_MANAGER.find(firstHandleA)->revision() != validRevision ||
        SHADER_MANAGER.find(firstHandleA)->name() != "Tests/First Reloaded") {
        return 12;
    }

    FILE_WATCHER.stop();
    if (!FILE_WATCHER.start(VirtualPath{"asset://"}, 100ms, false) ||
        !FILE_SYSTEM.writeText(VirtualPath{"asset://.ignored.tmp"}, "ignored") ||
        !FILE_SYSTEM.writeText(VirtualPath{"asset://watch.txt"}, "watch")) {
        return 13;
    }
    FILE_WATCHER.scanNow();
    if (!FILE_SYSTEM.writeText(VirtualPath{"asset://watch.txt"}, "watch changed")) {
        return 20;
    }
    FILE_WATCHER.scanNow();
    waitForEvents();
    auto events = FILE_WATCHER.pollEvents();
    if (events.size() != 1 || events.front().type != FileChangeType::Added) {
        return 14;
    }
    if (!FILE_SYSTEM.move(VirtualPath{"asset://watch.txt"}, VirtualPath{"asset://renamed.txt"})) {
        return 15;
    }
    FILE_WATCHER.scanNow();
    waitForEvents();
    events = FILE_WATCHER.pollEvents();
    if (events.size() != 1 || events.front().type != FileChangeType::Renamed ||
        events.front().previousPath != VirtualPath{"asset://watch.txt"} ||
        events.front().path != VirtualPath{"asset://renamed.txt"}) {
        return 16;
    }
    if (!FILE_SYSTEM.writeText(VirtualPath{"asset://renamed.txt"}, "modified contents")) {
        return 21;
    }
    FILE_WATCHER.scanNow();
    waitForEvents();
    events = FILE_WATCHER.pollEvents();
    if (events.size() != 1 || events.front().type != FileChangeType::Modified) {
        return 22;
    }
    if (!FILE_SYSTEM.removeFile(VirtualPath{"asset://renamed.txt"}))
        return 23;
    FILE_WATCHER.scanNow();
    waitForEvents();
    events = FILE_WATCHER.pollEvents();
    if (events.size() != 1 || events.front().type != FileChangeType::Removed) {
        return 24;
    }

    SHADER_MANAGER.destroy(firstHandleA);
    const ShaderHandle reused = SHADER_MANAGER.insert(shaderAssetA->instantiate());
    if (!reused || reused.index != firstHandleA.index ||
        reused.generation == firstHandleA.generation ||
        SHADER_MANAGER.find(firstHandleA) != nullptr) {
        return 17;
    }
}
