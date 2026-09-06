#include "asset/manager/AssetManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderGenerator.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "TestAssetEnvironment.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        engine::Log::fatal("ShaderGeneratorTest",
                           "Cannot open test fixture: " + path.string());
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
    using namespace engine;
    const std::filesystem::path sourceFixtures{MINI_TEST_SHADER_FIXTURE_DIR};
    const std::filesystem::path fixtures =
        std::filesystem::temp_directory_path() /
        "MiniEngineShaderGeneratorFixtures" / "assets";
    std::error_code fixtureError;
    std::filesystem::remove_all(fixtures.parent_path(), fixtureError);
    std::filesystem::create_directories(fixtures.parent_path(), fixtureError);
    std::filesystem::copy(sourceFixtures, fixtures,
                          std::filesystem::copy_options::recursive,
                          fixtureError);
    if (fixtureError) return 7;
    if (!test::initializeAssetEnvironment(fixtures)) return 8;
    const std::shared_ptr<ShaderAsset> shaderOwner = ASSET_MANAGER
        .loadAsset<ShaderAsset>(VirtualPath{"asset://material_values.shader.json"});
    if (!shaderOwner) return 5;
    const ShaderAsset& shader = *shaderOwner;
    UniformBlockLayout layout = buildUniformBlockLayout(shader.properties);
    const auto generated =
        shader_compiler::generateMaterialDeclarations(shader, layout);
    if (!generated) return 1;

    const std::string expected =
        readFile(sourceFixtures / "material_declarations.expected.glsl");
    if (generated->glsl != expected ||
        generated->uniformBlockSize != 80 || generated->textures.size() != 1 ||
        generated->textures[0].propertyName != "MainTexture" ||
        generated->textures[0].set != 1 || generated->textures[0].binding != 1) {
        return 1;
    }

    shader_compiler::ShaderGenerationOptions options;
    options.materialSet = 3;
    options.uniformBinding = 4;
    options.firstTextureBinding = 8;
    options.uniformBlockName = "CustomProperties";
    options.uniformInstanceName = "CustomMaterial";
    const auto custom =
        shader_compiler::generateMaterialDeclarations(shader, layout, options);
    if (!custom ||
        custom->glsl.find("set = 3, binding = 4") == std::string::npos ||
        custom->glsl.find("uniform CustomProperties") == std::string::npos ||
        custom->glsl.find("} CustomMaterial;") == std::string::npos ||
        custom->textures[0].binding != 8) {
        return 2;
    }
    options.uniformBlockName = "invalid name";
    if (shader_compiler::generateMaterialDeclarations(shader, layout, options)) {
        return 3;
    }

    const std::shared_ptr<ShaderAsset> interfaceShaderOwner = ASSET_MANAGER
        .loadAsset<ShaderAsset>(
            VirtualPath{"asset://shader_interface_valid.shader.json"});
    if (!interfaceShaderOwner) return 6;
    const ShaderAsset& interfaceShader = *interfaceShaderOwner;
    const ShaderPassDesc& interfacePass =
        interfaceShader.subShaders.front().passes.front().pass;
    const UniformBlockLayout interfaceLayout =
        buildUniformBlockLayout(interfaceShader.properties);
    const auto stages = shader_compiler::generatePassStages(
        interfaceShader, interfacePass, interfaceLayout,
        *FILE_SYSTEM.readText(interfacePass.program.vertexSource),
        *FILE_SYSTEM.readText(interfacePass.program.fragmentSource));
    std::string expectedVertex =
        readFile(sourceFixtures / "shader_interface.expected.vert.glsl");
    std::string expectedFragment =
        readFile(sourceFixtures / "shader_interface.expected.frag.glsl");
    expectedVertex.replace(expectedVertex.find("generated_interface.vert"),
                           std::string{"generated_interface.vert"}.size(),
                           interfacePass.program.vertexSource.string());
    expectedFragment.replace(expectedFragment.find("generated_interface.frag"),
                             std::string{"generated_interface.frag"}.size(),
                             interfacePass.program.fragmentSource.string());
    if (!stages || stages->vertexGlsl != expectedVertex ||
        stages->fragmentGlsl != expectedFragment) {
        return 4;
    }
    test::shutdownAssetEnvironment();
}
