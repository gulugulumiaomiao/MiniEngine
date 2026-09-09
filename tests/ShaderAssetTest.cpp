#include "asset/manager/AssetManager.h"
#include "render/shader/Shader.h"
#include "asset/importer/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <algorithm>
#include <filesystem>

int main() {
    using namespace engine;
    const std::filesystem::path assetRoot{MINI_TEST_ASSET_DIR};
    const std::filesystem::path fixtureRoot =
        std::filesystem::temp_directory_path() / "MiniEngineShaderAssetFixtures" / "assets";
    std::error_code fixtureError;
    std::filesystem::remove_all(fixtureRoot.parent_path(), fixtureError);
    std::filesystem::create_directories(fixtureRoot.parent_path(), fixtureError);
    std::filesystem::copy(MINI_TEST_SHADER_FIXTURE_DIR,
                          fixtureRoot,
                          std::filesystem::copy_options::recursive,
                          fixtureError);
    if (fixtureError)
        return 10;

    if (!test::initializeAssetEnvironment(assetRoot))
        return 11;
    const std::shared_ptr<ShaderAsset> vertexColorOwner = ASSET_MANAGER.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shaders/vertex_color.shader.json"});
    if (!vertexColorOwner)
        return 8;
    const ShaderAsset& vertexColor = *vertexColorOwner;
    const ShaderPassDesc& vertexColorPass =
        vertexColor.subShaders.front().requirePass(ShaderPassType::Forward);
    if (vertexColorPass.program.vertexSource.filename() != "vertex_color.Forward.vert" ||
        vertexColorPass.program.fragmentSource.filename() != "vertex_color.Forward.frag" ||
        vertexColorPass.vertexInput.size() != 2 || vertexColorPass.varyings.size() != 1 ||
        vertexColorPass.fragmentOutputs.size() != 1) {
        return 1;
    }

    FILE_WATCHER.stop();
    if (!test::initializeAssetEnvironment(fixtureRoot))
        return 12;
    const std::shared_ptr<ShaderAsset> generatedOwner = ASSET_MANAGER.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shader_interface_valid.shader.json"});
    if (!generatedOwner)
        return 9;
    const ShaderAsset& generated = *generatedOwner;
    const ShaderPassDesc& pass = generated.subShaders.front().requirePass(ShaderPassType::Forward);
    if (pass.program.vertexSource.filename() != "generated_interface.vert" ||
        pass.program.fragmentSource.filename() != "generated_interface.frag" ||
        pass.vertexInput.size() != 2 || pass.varyings.size() != 2 ||
        pass.fragmentOutputs.size() != 1 || pass.vertexInput[0].semantic != "POSITION" ||
        pass.vertexInput[0].type != ShaderValueType::Vec3 ||
        pass.varyings[1].interpolation != ShaderInterpolation::Flat) {
        return 2;
    }

    const RenderStateDesc& defaults = generated.subShaders.front().passes.front().renderState;
    if (defaults.cull != CullMode::Back || defaults.frontFace != FrontFace::Clockwise ||
        defaults.fill != FillMode::Solid || defaults.topology != PrimitiveTopology::TriangleList ||
        !defaults.depthWrite || defaults.depthTest != DepthCompare::LessEqual ||
        defaults.blend != BlendMode::Off || defaults.colorMask != "RGBA") {
        return 5;
    }

    if (ASSET_MANAGER.loadAsset<ShaderAsset>(
            VirtualPath{"asset://shader_interface_missing_entry.shader.json"})) {
        return 6;
    }
    test::shutdownAssetEnvironment();
}
