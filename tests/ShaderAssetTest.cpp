#include "renderer/AssetManager.h"
#include "renderer/Shader.h"

#include <algorithm>
#include <filesystem>

int main() {
    using namespace engine;
    const std::filesystem::path assetRoot{MINI_TEST_ASSET_DIR};
    const std::filesystem::path fixtureRoot{MINI_TEST_SHADER_FIXTURE_DIR};

    const ShaderAsset& vertexColor = *ASSET_MANAGER
        .loadShaderAsset(assetRoot / "shaders/vertex_color.shader.json");
    const ShaderPassDesc& vertexColorPass =
        vertexColor.subShaders.front().requirePass(ShaderPassType::Forward);
    if (vertexColorPass.program.vertexSource.filename() !=
            "vertex_color.Forward.vert" ||
        vertexColorPass.program.fragmentSource.filename() !=
            "vertex_color.Forward.frag" ||
        vertexColorPass.vertexInput.size() != 2 ||
        vertexColorPass.varyings.size() != 1 ||
        vertexColorPass.fragmentOutputs.size() != 1) {
        return 1;
    }

    const ShaderAsset& generated = *ASSET_MANAGER
        .loadShaderAsset(fixtureRoot / "shader_interface_valid.shader.json");
    const ShaderPassDesc& pass =
        generated.subShaders.front().requirePass(ShaderPassType::Forward);
    if (pass.program.vertexSource.filename() != "generated_interface.vert" ||
        pass.program.fragmentSource.filename() != "generated_interface.frag" ||
        pass.vertexInput.size() != 2 ||
        pass.varyings.size() != 2 || pass.fragmentOutputs.size() != 1 ||
        pass.vertexInput[0].semantic != "POSITION" ||
        pass.vertexInput[0].type != ShaderValueType::Vec3 ||
        pass.varyings[1].interpolation != ShaderInterpolation::Flat) {
        return 2;
    }

    const std::filesystem::path generatedVertex =
        std::filesystem::path{MINI_TEST_GENERATED_SHADER_DIR} /
        "vertex_color.Forward.vert.reflection.spv";
    const std::filesystem::path generatedFragment =
        std::filesystem::path{MINI_TEST_GENERATED_SHADER_DIR} /
        "vertex_color.Forward.frag.reflection.spv";
    const auto vertexReflectionResult =
        reflectSpirv(VirtualPath::fromNative(generatedVertex));
    const auto fragmentReflectionResult =
        reflectSpirv(VirtualPath::fromNative(generatedFragment));
    if (!vertexReflectionResult || !fragmentReflectionResult) {
        return 3;
    }
    const SpirvReflection& vertexReflection = *vertexReflectionResult;
    const SpirvReflection& fragmentReflection = *fragmentReflectionResult;
    const auto materialBlock = std::ranges::find_if(
        fragmentReflection.descriptors,
        [](const ShaderDescriptorBinding& descriptor) {
            return descriptor.set == 1 && descriptor.binding == 0;
        });
    if (vertexReflection.stage != ShaderStage::Vertex ||
        fragmentReflection.stage != ShaderStage::Fragment ||
        vertexReflection.inputs.size() != 2 ||
        vertexReflection.outputs.size() != 1 ||
        fragmentReflection.inputs.size() != 1 ||
        fragmentReflection.outputs.size() != 1 ||
        materialBlock == fragmentReflection.descriptors.end() ||
        materialBlock->type != ShaderDescriptorType::UniformBuffer ||
        materialBlock->members.size() != 4 ||
        materialBlock->members[0].offset != 0 ||
        materialBlock->members[1].offset != 16 ||
        materialBlock->members[2].offset != 32 ||
        materialBlock->members[3].offset != 48) {
        return 3;
    }
    if (!validateSpirvReflection(vertexColor, vertexColorPass,
                                 VirtualPath::fromNative(generatedVertex),
                                 VirtualPath::fromNative(generatedFragment))) {
        return 4;
    }

    const RenderStateDesc& defaults =
        generated.subShaders.front().passes.front().renderState;
    if (defaults.cull != CullMode::Back ||
        defaults.frontFace != FrontFace::Clockwise ||
        defaults.fill != FillMode::Solid ||
        defaults.topology != PrimitiveTopology::TriangleList ||
        !defaults.depthWrite || defaults.depthTest != DepthCompare::LessEqual ||
        defaults.blend != BlendMode::Off || defaults.colorMask != "RGBA") {
        return 5;
    }

    if (ASSET_MANAGER.loadShaderAsset(
            fixtureRoot / "shader_interface_missing_entry.shader.json")) {
        return 6;
    }
    if (reflectSpirv(VirtualPath::fromNative(fixtureRoot / "missing.spv"))) {
        return 7;
    }

}
