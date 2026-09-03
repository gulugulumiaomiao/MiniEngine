#include "asset/AssetArtifact.h"
#include "asset/AssetDatabase.h"
#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/BuiltinAssetImporters.h"
#include "core/io/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

int main() {
    using namespace engine;

    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mini-vulkan-importer-test-" + std::to_string(unique));
    const std::filesystem::path assetRoot = root / "assets";
    const std::filesystem::path libraryRoot = root / "library";
    std::error_code error;
    std::filesystem::create_directories(assetRoot, error);
    std::filesystem::create_directories(libraryRoot, error);
    if (error || !FILE_SYSTEM.mountDirectory("asset", assetRoot) ||
        !FILE_SYSTEM.mountDirectory("library", libraryRoot)) {
        return 1;
    }

    const VirtualPath shaderPath{"asset://shaders/import_test.shader.json"};
    const VirtualPath vertexPath{"asset://shaders/import_test.vert"};
    const VirtualPath fragmentPath{"asset://shaders/import_test.frag"};
    const VirtualPath commonPath{"asset://shaders/include/common.glsl"};
    const VirtualPath nestedPath{"asset://shaders/include/nested.glsl"};
    const std::string shaderSource = R"({
  "$schemaVersion": 1,
  "name": "Importer/Test",
  "subShaders": [{
    "passes": [{
      "name": "Forward",
      "lightMode": "Forward",
      "program": {
        "vertex": "import_test.vert",
        "frag": "import_test.frag"
      }
    }]
  }]
})";
    if (!FILE_SYSTEM.writeText(shaderPath, shaderSource) ||
        !FILE_SYSTEM.writeText(vertexPath,
                               "#include \"include/common.glsl\"\nvoid main() {}\n") ||
        !FILE_SYSTEM.writeText(fragmentPath, "void main() {}\n") ||
        !FILE_SYSTEM.writeText(commonPath,
                               "#include \"nested.glsl\"\n") ||
        !FILE_SYSTEM.writeText(nestedPath, "const float nested = 1.0;\n")) {
        return 2;
    }

    AssetImporterRegistry registry;
    if (!registerBuiltinAssetImporters(registry) ||
        !registry.find(AssetType::Shader) ||
        !registry.find(AssetType::Material) ||
        registry.find(AssetType::Unknown)) {
        return 3;
    }

    const AssetMeta meta{1, AssetId::generate(), AssetType::Shader};
    const VirtualPath artifactPath = ASSET_DATABASE.artifactPath(meta.assetId);
    const AssetImportContext context{meta,
                                     shaderPath,
                                     assetMetaPath(shaderPath),
                                     artifactPath,
                                     {}};
    const AssetImportResult result =
        registry.find(AssetType::Shader)->import(context);
    if (!result.success || result.type != AssetType::Shader ||
        result.artifactPath.string() != artifactPath.string() ||
        result.dependencies.size() != 4) {
        return 4;
    }

    const auto contains = [&result](const VirtualPath& expected) {
        return std::ranges::find_if(
                   result.dependencies,
                   [&expected](const VirtualPath& dependency) {
                       return dependency.string() == expected.string();
                   }) != result.dependencies.end();
    };
    if (!contains(vertexPath) || !contains(fragmentPath) ||
        !contains(commonPath) || !contains(nestedPath)) {
        return 5;
    }

    const auto artifact = loadAssetArtifact(artifactPath);
    if (!artifact || artifact->assetId != meta.assetId ||
        artifact->assetType != AssetType::Shader ||
        artifact->payload.find("Importer/Test") == std::string::npos) {
        return 6;
    }

    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
    std::filesystem::remove_all(root, error);
    return error ? 7 : 0;
}
