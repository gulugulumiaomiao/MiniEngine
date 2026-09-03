#include "renderer/AssetManager.h"
#include "renderer/Shader.h"
#include "renderer/ShaderGenerator.h"
#include "core/Log.h"

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
    const std::filesystem::path fixtures{MINI_TEST_SHADER_FIXTURE_DIR};
    const ShaderAsset& shader = *ASSET_MANAGER
        .loadShaderAsset(fixtures / "material_values.shader.json");
    UniformBlockLayout layout = buildUniformBlockLayout(shader.properties);
    const auto generated =
        shader_compiler::generateMaterialDeclarations(shader, layout);
    if (!generated) return 1;

    const std::string expected =
        readFile(fixtures / "material_declarations.expected.glsl");
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

    const ShaderAsset& interfaceShader = *ASSET_MANAGER
        .loadShaderAsset(fixtures / "shader_interface_valid.shader.json");
    const ShaderPassDesc& interfacePass =
        interfaceShader.subShaders.front().passes.front().pass;
    const UniformBlockLayout interfaceLayout =
        buildUniformBlockLayout(interfaceShader.properties);
    const auto stages = shader_compiler::generatePassStages(
        interfaceShader, interfacePass, interfaceLayout,
        *FILE_SYSTEM.readText(interfacePass.program.vertexSource),
        *FILE_SYSTEM.readText(interfacePass.program.fragmentSource));
    std::string expectedVertex =
        readFile(fixtures / "shader_interface.expected.vert.glsl");
    std::string expectedFragment =
        readFile(fixtures / "shader_interface.expected.frag.glsl");
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

}
