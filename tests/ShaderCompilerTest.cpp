#include "asset/manager/AssetManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "render/shader/ShaderGenerator.h"
#include "render/shader/ShaderPreprocessor.h"
#include "core/filesystem/FileSystem.h"
#include "TestAssetEnvironment.h"

#include <filesystem>

int main() {
    using namespace engine;

    const std::filesystem::path fixtures{MINI_TEST_SHADER_FIXTURE_DIR};
    if (!FILE_SYSTEM.mountDirectory("fixture", fixtures, true))
        return 12;
    ShaderPreprocessor preprocessor{{{VirtualPath{"fixture://"}}}};
    ShaderPreprocessRequest request;
    request.source.sourcePath = VirtualPath{"fixture://preprocess_root.glsl"};
    request.source.stage = ShaderStage::Fragment;
    request.source.source = *FILE_SYSTEM.readText(request.source.sourcePath);
    request.defines.push_back({"TEST_VALUE", "0.5"});
    const auto processed = preprocessor.process(request);
    if (!processed || processed->dependencies.size() != 2 ||
        !processed->source.starts_with("#version 450\n#define TEST_VALUE 0.5") ||
        processed->source.find("BuildColor") == std::string::npos || processed->sourceHash == 0) {
        return 1;
    }
    if (preprocessor.process({})) {
        return 7;
    }
    ShaderPreprocessRequest searchRequest;
    searchRequest.source.sourcePath = VirtualPath{"fixture://preprocess_search_root.glsl"};
    searchRequest.source.stage = ShaderStage::Fragment;
    searchRequest.source.source = *FILE_SYSTEM.readText(searchRequest.source.sourcePath);
    const auto searched = preprocessor.process(searchRequest);
    if (!searched || searched->dependencies.size() != 2 ||
        searched->source.find("BuildColor") == std::string::npos) {
        return 13;
    }

    const ShaderKeywordSchema schema{{"NORMAL_MAP", "ALPHA_TEST"}};
    const std::vector<std::string> enabled{"NORMAL_MAP"};
    const ShaderVariantKey variant = schema.makeKey(enabled, 2, 4);
    if (!schema.declares("ALPHA_TEST") || variant.keywordBits != 2 ||
        variant.meshFeatureBits != 2 || variant.platformFeatureBits != 4) {
        return 2;
    }

    if (!FILE_SYSTEM.mountDirectory("shader", MINI_TEST_GENERATED_SHADER_DIR, false)) {
        return 8;
    }
    if (!test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR))
        return 11;
    const std::shared_ptr<ShaderAsset> assetOwner = ASSET_MANAGER.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shaders/vertex_color.shader.json"});
    if (!assetOwner)
        return 10;
    const ShaderAsset& asset = *assetOwner;
    const Shader runtimeShader{asset};
    const ShaderPass& pass = runtimeShader.defaultSubShader().requirePass(ShaderPassType::Forward);
    ShaderCompilePipeline pipeline;
    const ShaderProgramHandle programHandle = pipeline.getOrCreate(runtimeShader, pass);
    const ShaderProgram& program = pipeline.resolve(programHandle);
    const CompiledShader& vertex = pipeline.resolve(program.vertex);
    const CompiledShader& fragment = pipeline.resolve(program.fragment);
    if (vertex.stage != ShaderStage::Vertex || fragment.stage != ShaderStage::Fragment ||
        program.layout.vertexInputs.size() != 2 || program.layout.fragmentOutputs.size() != 1 ||
        program.layout.descriptors.empty() || program.layout.id == 0) {
        return 3;
    }
    if (pipeline.getOrCreate(runtimeShader, pass) != programHandle) {
        return 4;
    }
    ShaderCompilePipelineConfig packagedConfig;
    packagedConfig.mode = ShaderCompileMode::PackagedRuntime;
    ShaderCompilePipeline packagedPipeline{std::move(packagedConfig)};
    const ShaderProgramHandle packagedHandle = packagedPipeline.getOrCreate(runtimeShader, pass);
    if (!packagedHandle ||
        packagedPipeline.resolve(packagedHandle).layout.id != program.layout.id) {
        return 14;
    }

    const ShaderPassDesc& assetPass = asset.subShaders.front().requirePass(ShaderPassType::Forward);
    ShaderPreprocessRequest generatedRequest;
    generatedRequest.source.sourcePath = assetPass.program.vertexSource;
    generatedRequest.source.stage = ShaderStage::Vertex;
    ShaderGenerator generator;
    generatedRequest.source.source =
        *generator.generateStage(runtimeShader,
                                 pass,
                                 ShaderStage::Vertex,
                                 *FILE_SYSTEM.readText(assetPass.program.vertexSource));
    const auto generated = preprocessor.process(generatedRequest);
    if (!generated || generated->dependencies.empty() ||
        generated->source.find("struct MiniVertexInput") == std::string::npos ||
        generated->source.find("void main()") == std::string::npos) {
        return 9;
    }
    const std::vector<CompiledShaderId> invalidated = pipeline.invalidate(vertex.binaryPath);
    if (invalidated.size() != 1 || invalidated.front() != vertex.id) {
        return 6;
    }
    test::shutdownAssetEnvironment();
    (void)FILE_SYSTEM.unmount("fixture");
}
