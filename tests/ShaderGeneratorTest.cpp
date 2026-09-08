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
        engine::Log::fatal("ShaderGeneratorTest", "Cannot open test fixture: " + path.string());
    }
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
    using namespace engine;
    const std::filesystem::path sourceFixtures{MINI_TEST_SHADER_FIXTURE_DIR};
    const std::filesystem::path fixtures =
        std::filesystem::temp_directory_path() / "MiniEngineShaderGeneratorFixtures" / "assets";
    std::error_code fixtureError;
    std::filesystem::remove_all(fixtures.parent_path(), fixtureError);
    std::filesystem::create_directories(fixtures.parent_path(), fixtureError);
    std::filesystem::copy(
        sourceFixtures, fixtures, std::filesystem::copy_options::recursive, fixtureError);
    if (fixtureError)
        return 7;
    if (!test::initializeAssetEnvironment(fixtures))
        return 8;
    const std::shared_ptr<ShaderAsset> shaderOwner =
        ASSET_MANAGER.loadAsset<ShaderAsset>(VirtualPath{"asset://material_values.shader.json"});
    if (!shaderOwner)
        return 5;
    const Shader shader{*shaderOwner};
    const ShaderPass& materialPass = shader.defaultSubShader().requirePass(ShaderPassType::Forward);
    ShaderGenerator generator;
    const auto generated = generator.generateStage(
        shader,
        materialPass,
        ShaderStage::Vertex,
        "void VertexMain(MiniVertexInput inputValue, out MiniVaryings outputValue) {}\n");
    const std::string expected = readFile(sourceFixtures / "material_declarations.expected.glsl");
    if (!generated || generated->find(expected) == std::string::npos) {
        return 1;
    }

    const std::shared_ptr<ShaderAsset> interfaceShaderOwner = ASSET_MANAGER.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shader_interface_valid.shader.json"});
    if (!interfaceShaderOwner)
        return 6;
    const Shader interfaceShader{*interfaceShaderOwner};
    const ShaderPass& interfacePass =
        interfaceShader.defaultSubShader().requirePass(ShaderPassType::Forward);
    const auto vertex =
        generator.generateStage(interfaceShader,
                                interfacePass,
                                ShaderStage::Vertex,
                                *FILE_SYSTEM.readText(interfacePass.program().vertexSource));
    const auto fragment =
        generator.generateStage(interfaceShader,
                                interfacePass,
                                ShaderStage::Fragment,
                                *FILE_SYSTEM.readText(interfacePass.program().fragmentSource));
    std::string expectedVertex = readFile(sourceFixtures / "shader_interface.expected.vert.glsl");
    std::string expectedFragment = readFile(sourceFixtures / "shader_interface.expected.frag.glsl");
    expectedVertex.replace(expectedVertex.find("generated_interface.vert"),
                           std::string{"generated_interface.vert"}.size(),
                           interfacePass.program().vertexSource.string());
    expectedFragment.replace(expectedFragment.find("generated_interface.frag"),
                             std::string{"generated_interface.frag"}.size(),
                             interfacePass.program().fragmentSource.string());
    if (!vertex || !fragment || *vertex != expectedVertex || *fragment != expectedFragment) {
        return 4;
    }
    test::shutdownAssetEnvironment();
}
