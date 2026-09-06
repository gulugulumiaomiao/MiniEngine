#include "asset/derived_data/AssetArtifact.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/BuiltinAssetImporters.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/filesystem/FileSystem.h"
#include "render/mesh/Mesh.h"
#include "render/shader/Shader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

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
                               "#include \"asset://shaders/include/nested.glsl\"\n") ||
        !FILE_SYSTEM.writeText(nestedPath, "const float nested = 1.0;\n")) {
        return 2;
    }

    AssetImporterRegistry registry;
    if (!registerBuiltinAssetImporters(registry) ||
        !registry.find(AssetType::Shader) ||
        !registry.find(AssetType::Material) ||
        !registry.find(AssetType::Mesh) ||
        !registry.find(AssetType::Scene) ||
        registry.find(AssetType::Unknown)) {
        return 3;
    }

    const AssetMeta meta{1, AssetId::generate(), AssetType::Shader};
    const VirtualPath artifactPath = ASSET_DATABASE.artifactPath(meta.assetId);
    const AssetImportContext context{meta,
                                     shaderPath,
                                     assetMetaPath(shaderPath),
                                     artifactPath};
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
        artifact->assetType != AssetType::Shader) {
        return 6;
    }

    const VirtualPath meshPath{"asset://meshes/import_test.mesh.json"};
    constexpr std::array positions{
        math::Vec3{-1.0F, -1.0F, 0.0F},
        math::Vec3{1.0F, -1.0F, 0.0F},
        math::Vec3{0.0F, 1.0F, 0.0F},
    };
    nlohmann::json meshJson{
        {"name", "ImporterMesh"},
        {"index_type", "uint16"},
        {"bindings", {{{"binding", 0}, {"stride", sizeof(math::Vec3)}}}},
        {"attributes", {{{"semantic", "position"},
                           {"format", "vec3_float32"}, {"location", 0},
                           {"binding", 0}, {"offset", 0}}}},
        {"indices", {0, 1, 2}},
    };
    std::vector<std::uint8_t> positionBytes;
    for (const std::byte value : std::as_bytes(std::span{positions})) {
        positionBytes.push_back(std::to_integer<std::uint8_t>(value));
    }
    meshJson["vertex_streams"] = {{{"binding", 0}, {"vertex_count", 3},
                                    {"bytes", positionBytes}}};
    if (!FILE_SYSTEM.writeText(meshPath, meshJson.dump())) return 7;
    const AssetMeta meshMeta{1, AssetId::generate(), AssetType::Mesh};
    const VirtualPath meshArtifactPath = ASSET_DATABASE.artifactPath(meshMeta.assetId);
    const AssetImportContext meshContext{meshMeta, meshPath,
        assetMetaPath(meshPath), meshArtifactPath};
    const AssetImportResult meshResult =
        registry.find(AssetType::Mesh)->import(meshContext);
    const auto meshArtifact = loadAssetArtifact(meshArtifactPath);
    MeshAsset mesh;
    mesh.setAssetPath(meshPath);
    BinaryReader meshReader{meshArtifact ? meshArtifact->payload
                                         : std::span<const std::byte>{}};
    if (!meshResult.success || !meshArtifact ||
        meshArtifact->assetType != AssetType::Mesh ||
        !mesh.transfer(meshReader) || !meshReader.finished() ||
        mesh.desc.debugName != "ImporterMesh" ||
        mesh.meshData.indexCount != 3) {
        return 8;
    }
    ShaderAsset shader;
    shader.setAssetPath(shaderPath);
    BinaryReader shaderReader{artifact->payload};
    if (!shader.transfer(shaderReader) || !shaderReader.finished() ||
        shader.name != "Importer/Test" ||
        shader.subShaders.size() != 1 ||
        shader.subShaders.front().passes.size() != 1) {
        return 6;
    }

    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
    std::filesystem::remove_all(root, error);
    return error ? 9 : 0;
}
