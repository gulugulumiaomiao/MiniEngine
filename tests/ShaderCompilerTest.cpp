#include "renderer/AssetManager.h"
#include "renderer/Shader.h"
#include "renderer/ShaderCompiler.h"
#include "core/io/FileSystem.h"

#include <filesystem>

int main() {
    using namespace engine;

    const std::filesystem::path fixtures{MINI_TEST_SHADER_FIXTURE_DIR};
    ShaderPreprocessor preprocessor;
    ShaderCompileRequest request;
    request.source = VirtualPath::fromNative(fixtures / "preprocess_root.glsl");
    request.stage = ShaderStage::Fragment;
    request.defines.push_back({"TEST_VALUE", "0.5"});
    const auto processed = preprocessor.process(request);
    if (!processed || processed->dependencies.size() != 2 ||
        !processed->source.starts_with("#version 450\n#define TEST_VALUE 0.5") ||
        processed->source.find("BuildColor") == std::string::npos ||
        processed->sourceHash == 0) {
        return 1;
    }
    if (preprocessor.process({})) {
        return 7;
    }

    const ShaderKeywordSchema schema{{"NORMAL_MAP", "ALPHA_TEST"}};
    const std::vector<std::string> enabled{"NORMAL_MAP"};
    const ShaderVariantKey variant = schema.makeKey(enabled, 2, 4);
    if (!schema.declares("ALPHA_TEST") || variant.keywordBits != 2 ||
        variant.meshFeatureBits != 2 || variant.platformFeatureBits != 4) {
        return 2;
    }

    if (!FILE_SYSTEM.mountDirectory("shader", MINI_TEST_GENERATED_SHADER_DIR,
                                    false)) {
        return 8;
    }
    const ShaderAsset& asset = *ASSET_MANAGER.loadShaderAsset(
        std::filesystem::path{MINI_TEST_ASSET_DIR} /
        "shaders/vertex_color.shader.json");
    const Shader runtimeShader{asset};
    const ShaderPass& pass = runtimeShader.defaultSubShader().requirePass(
        ShaderPassType::Forward);
    CompiledShaderCache compiledShaders;
    ShaderProgramCache programs{compiledShaders};
    const ShaderProgramHandle programHandle =
        programs.getOrCreate(runtimeShader, pass);
    const ShaderProgram& program = programs.resolve(programHandle);
    const CompiledShader& vertex = compiledShaders.resolve(program.vertex);
    const CompiledShader& fragment = compiledShaders.resolve(program.fragment);
    if (vertex.stage != ShaderStage::Vertex ||
        fragment.stage != ShaderStage::Fragment ||
        program.layout.vertexInputs.size() != 2 ||
        program.layout.fragmentOutputs.size() != 1 ||
        program.layout.descriptors.empty() || program.layout.id == 0) {
        return 3;
    }
    if (programs.getOrCreate(runtimeShader, pass) != programHandle) {
        return 4;
    }

    const ShaderPassDesc& assetPass =
        asset.subShaders.front().requirePass(ShaderPassType::Forward);
    ShaderCompileRequest generatedRequest;
    generatedRequest.source = assetPass.program.vertexSource;
    generatedRequest.stage = ShaderStage::Vertex;
    generatedRequest.shaderAsset = &asset;
    generatedRequest.shaderPass = &assetPass;
    const auto generated = preprocessor.process(generatedRequest);
    if (!generated || generated->dependencies.empty() ||
        generated->source.find("struct MiniVertexInput") == std::string::npos ||
        generated->source.find("void main()") == std::string::npos) {
        return 9;
    }
    const std::pair<const ShaderPass*, ShaderProgram> programPair{&pass, program};
    const ShaderCookedAsset cooked = buildCookedShaderAsset(asset, {&programPair, 1});
    if (cooked.name != asset.name || cooked.materialLayout.byteSize != 64 ||
        cooked.passes.empty() || cooked.passes.front().variants.size() != 1 ||
        cooked.passes.front().variants.front().vertex != vertex.id) {
        return 5;
    }

    const std::vector<CompiledShaderId> invalidated =
        compiledShaders.invalidateDependency(vertex.dependencies.front());
    if (invalidated.size() != 1 || invalidated.front() != vertex.id) {
        return 6;
    }
    programs.invalidate(invalidated);
}
