#include "asset/importer/AssetImportPipeline.h"
#include "asset/importer/FileWatcher.h"
#include "asset/manager/AssetManager.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"
#include "TestAssetEnvironment.h"

#include <filesystem>

int main() {
    using namespace engine;
    if (!test::initializeAssetEnvironment(MINI_TEST_COOKED_ASSET_DIR, true)) {
        return 5;
    }
    if (FILE_WATCHER.running() || ASSET_IMPORT_PIPELINE.initialized()) return 1;

    const auto shader = ASSET_MANAGER.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shaders/vertex_color.shader.json"});
    const auto material = ASSET_MANAGER.loadAsset<MaterialAsset>(
        VirtualPath{"asset://materials/warm_vertex_color.material.json"});
    if (!shader || !material) return 2;
    if (ASSET_MANAGER.loadAsset<ShaderAsset>(
            VirtualPath{"asset://shaders/missing.shader.json"})) {
        return 3;
    }

    const MaterialHandle runtime = MATERIAL_MANAGER.load(material->assetPath());
    if (!runtime || MATERIAL_MANAGER.find(runtime)->shader().name() !=
                        "MiniEngine/VertexColor") {
        return 4;
    }
    MATERIAL_MANAGER.clear();
    SHADER_MANAGER.clear();
    test::shutdownAssetEnvironment();
}
