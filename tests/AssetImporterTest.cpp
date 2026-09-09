#include "asset/database/AssetDatabase.h"
#include "asset/derived_data/AssetArtifact.h"
#include "asset/importer/AssetImporterRegistry.h"
#include "asset/importer/BuiltinAssetImporters.h"
#include "core/filesystem/FileSystem.h"
#include "core/serialization/BinaryTransfer.h"
#include "render/mesh/Mesh.h"
#include "render/shader/Shader.h"
#include "render/texture/Texture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> readFixture(std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{MINI_TEST_SOURCE_DIR} / "tests" / "data" / name;
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    std::vector<std::byte> result;
    if (!stream || stream.tellg() <= 0)
        return result;
    result.resize(static_cast<std::size_t>(stream.tellg()));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()),
                static_cast<std::streamsize>(result.size()));
    if (!stream)
        result.clear();
    return result;
}

} // namespace

int main() {
    using namespace engine;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
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
        !FILE_SYSTEM.writeText(vertexPath, "#include \"include/common.glsl\"\nvoid main() {}\n") ||
        !FILE_SYSTEM.writeText(fragmentPath, "void main() {}\n") ||
        !FILE_SYSTEM.writeText(commonPath, "#include \"asset://shaders/include/nested.glsl\"\n") ||
        !FILE_SYSTEM.writeText(nestedPath, "const float nested = 1.0;\n")) {
        return 2;
    }

    AssetImporterRegistry registry;
    if (!registerBuiltinAssetImporters(registry) || !registry.find(AssetType::Shader) ||
        !registry.find(AssetType::Material) || !registry.find(AssetType::Mesh) ||
        !registry.find(AssetType::Texture) || !registry.find(AssetType::Scene) ||
        registry.find(AssetType::Unknown)) {
        return 3;
    }

    const AssetMeta meta{1, AssetId::generate(), AssetType::Shader};
    const VirtualPath artifactPath = ASSET_DATABASE.artifactPath(meta.assetId);
    const AssetImportContext context{meta, shaderPath, assetMetaPath(shaderPath), artifactPath};
    const AssetImportResult result = registry.find(AssetType::Shader)->import(context);
    if (!result.success || result.type != AssetType::Shader ||
        result.artifactPath.string() != artifactPath.string() || !result.dependencies.empty()) {
        return 4;
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
        {"attributes",
         {{{"semantic", "position"},
           {"format", "vec3_float32"},
           {"location", 0},
           {"binding", 0},
           {"offset", 0}}}},
        {"indices", {0, 1, 2}},
    };
    std::vector<std::uint8_t> positionBytes;
    for (const std::byte value : std::as_bytes(std::span{positions})) {
        positionBytes.push_back(std::to_integer<std::uint8_t>(value));
    }
    meshJson["vertex_streams"] = {{{"binding", 0}, {"vertex_count", 3}, {"bytes", positionBytes}}};
    if (!FILE_SYSTEM.writeText(meshPath, meshJson.dump()))
        return 7;
    const AssetMeta meshMeta{1, AssetId::generate(), AssetType::Mesh};
    const VirtualPath meshArtifactPath = ASSET_DATABASE.artifactPath(meshMeta.assetId);
    const AssetImportContext meshContext{
        meshMeta, meshPath, assetMetaPath(meshPath), meshArtifactPath};
    const AssetImportResult meshResult = registry.find(AssetType::Mesh)->import(meshContext);
    const auto meshArtifact = loadAssetArtifact(meshArtifactPath);
    MeshAsset mesh;
    mesh.setAssetPath(meshPath);
    BinaryReader meshReader{meshArtifact ? meshArtifact->payload : std::span<const std::byte>{}};
    if (!meshResult.success || !meshArtifact || meshArtifact->assetType != AssetType::Mesh ||
        !mesh.transfer(meshReader) || !meshReader.finished() ||
        mesh.desc.debugName != "ImporterMesh" || mesh.meshData.indexCount != 3) {
        return 8;
    }

    const VirtualPath proceduralPath{"asset://meshes/procedural_test.mesh.json"};
    const nlohmann::json proceduralJson{
        {"name", "ProceduralImporterMesh"},
        {"keep_cpu_copy", true},
        {"source",
         {{"type", "procedural"},
          {"vertex_layout", "position_normal_tangent_uv"},
          {"index_policy", "auto"},
          {"parts",
           {
               {{"type", "box"},
                {"parameters", {{"size", {2.0, 1.0, 1.0}}}},
                {"translation", {-1.0, 0.0, 0.0}},
                {"material_slot", 4}},
               {{"type", "sphere"},
                {"parameters",
                 {{"radius", 0.5}, {"longitude_segments", 8}, {"latitude_segments", 4}}},
                {"translation", {1.0, 0.0, 0.0}},
                {"material_slot", 7}},
           }}}}};
    if (!FILE_SYSTEM.writeText(proceduralPath, proceduralJson.dump()))
        return 10;
    const AssetMeta proceduralMeta{1, AssetId::generate(), AssetType::Mesh};
    const VirtualPath proceduralArtifactPath = ASSET_DATABASE.artifactPath(proceduralMeta.assetId);
    const AssetImportContext proceduralContext{
        proceduralMeta, proceduralPath, assetMetaPath(proceduralPath), proceduralArtifactPath};
    const AssetImportResult proceduralResult =
        registry.find(AssetType::Mesh)->import(proceduralContext);
    const auto proceduralArtifact = loadAssetArtifact(proceduralArtifactPath);
    MeshAsset proceduralMesh;
    BinaryReader proceduralReader{proceduralArtifact ? proceduralArtifact->payload
                                                     : std::span<const std::byte>{}};
    if (!proceduralResult.success || !proceduralArtifact ||
        !proceduralMesh.transfer(proceduralReader) || !proceduralReader.finished() ||
        !proceduralMesh.buildRecipe || proceduralMesh.buildRecipe->parts.size() != 2 ||
        proceduralMesh.desc.subMeshes.size() != 2 ||
        proceduralMesh.desc.subMeshes[0].materialSlot != 4 ||
        proceduralMesh.desc.subMeshes[1].materialSlot != 7 ||
        proceduralMesh.meshData.vertexStreams.front().vertexCount != 69) {
        return 10;
    }
    ShaderAsset shader;
    shader.setAssetPath(shaderPath);
    BinaryReader shaderReader{artifact->payload};
    if (!shader.transfer(shaderReader) || !shaderReader.finished() ||
        shader.name != "Importer/Test" || shader.subShaders.size() != 1 ||
        shader.subShaders.front().passes.size() != 1) {
        return 6;
    }

    const VirtualPath texturePath{"asset://textures/import_test.ktx"};
    std::vector<std::byte> ktx(84U);
    constexpr std::array<std::uint8_t, 12> ktxIdentifier{
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    for (std::size_t index = 0; index < ktxIdentifier.size(); ++index)
        ktx[index] = static_cast<std::byte>(ktxIdentifier[index]);
    const auto writeU32 = [&ktx](std::size_t offset, std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte)
            ktx[offset + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xffU);
    };
    writeU32(12, 0x04030201U);
    writeU32(16, 0x1401U);
    writeU32(20, 1U);
    writeU32(24, 0x1908U);
    writeU32(28, 0x8058U);
    writeU32(32, 0x1908U);
    writeU32(36, 2U);
    writeU32(40, 2U);
    writeU32(44, 0U);
    writeU32(48, 0U);
    writeU32(52, 1U);
    writeU32(56, 1U);
    writeU32(60, 0U);
    writeU32(64, 16U);
    for (std::size_t index = 68; index < ktx.size(); ++index)
        ktx[index] = static_cast<std::byte>(index);
    if (!FILE_SYSTEM.writeBinary(texturePath, ktx) ||
        inferAssetType(VirtualPath{"asset://textures/color.png"}) != AssetType::Texture ||
        inferAssetType(VirtualPath{"asset://textures/color.jpg"}) != AssetType::Texture ||
        inferAssetType(VirtualPath{"asset://textures/color.ktx2"}) != AssetType::Texture) {
        return 11;
    }
    const AssetMeta textureMeta{1, AssetId::generate(), AssetType::Texture};
    const VirtualPath textureArtifactPath = ASSET_DATABASE.artifactPath(textureMeta.assetId);
    const AssetImportContext textureContext{
        textureMeta, texturePath, assetMetaPath(texturePath), textureArtifactPath};
    const AssetImportResult textureResult =
        registry.find(AssetType::Texture)->import(textureContext);
    const auto textureArtifact = loadAssetArtifact(textureArtifactPath);
    TextureAsset texture;
    BinaryReader textureReader{textureArtifact ? textureArtifact->payload
                                               : std::span<const std::byte>{}};
    if (!textureResult.success || !textureArtifact ||
        textureArtifact->assetType != AssetType::Texture || !texture.transfer(textureReader) ||
        !textureReader.finished() || texture.desc.type != TextureType::Texture2D ||
        texture.desc.format != TextureFormat::Rgba8Unorm || texture.desc.width != 2 ||
        texture.desc.height != 2 || texture.desc.mipCount != 1 || texture.mipData.size() != 1 ||
        texture.mipData.front().bytes.size() != 16) {
        return 12;
    }

    const std::vector<std::byte> png = readFixture("texture-test.png");
    const std::vector<std::byte> jpg = readFixture("texture-test.jpg");
    if (png.empty())
        return 15;
    if (jpg.empty())
        return 16;
    const auto verifyImageImport = [&](const VirtualPath& sourcePath,
                                       std::span<const std::byte> sourceBytes) {
        if (sourceBytes.empty() || !FILE_SYSTEM.writeBinary(sourcePath, sourceBytes))
            return false;
        const AssetMeta meta{1, AssetId::generate(), AssetType::Texture};
        const VirtualPath artifactPath = ASSET_DATABASE.artifactPath(meta.assetId);
        const AssetImportContext context{meta, sourcePath, assetMetaPath(sourcePath), artifactPath};
        const AssetImportResult result = registry.find(AssetType::Texture)->import(context);
        const auto artifact = loadAssetArtifact(artifactPath);
        TextureAsset asset;
        BinaryReader reader{artifact ? artifact->payload : std::span<const std::byte>{}};
        return result.success && artifact && asset.transfer(reader) && reader.finished() &&
               asset.desc.width == 2 && asset.desc.height == 2 && asset.desc.mipCount == 2 &&
               asset.desc.format == TextureFormat::Rgba8Srgb && asset.mipData.size() == 2 &&
               asset.mipData[0].bytes.size() == 16 && asset.mipData[1].bytes.size() == 4;
    };
    if (!verifyImageImport(VirtualPath{"asset://textures/import_test.png"}, png)) {
        return 13;
    }
    if (!verifyImageImport(VirtualPath{"asset://textures/import_test.jpg"}, jpg))
        return 14;

    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
    std::filesystem::remove_all(root, error);
    return error ? 9 : 0;
}
