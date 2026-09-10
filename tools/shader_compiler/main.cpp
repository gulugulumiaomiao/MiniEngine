#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompilePipeline.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

class AssetRuntimeScope final {
public:
    ~AssetRuntimeScope() {
        ASSET_MANAGER.shutdown();
        (void)FILE_SYSTEM.unmount("shader-bin");
        (void)FILE_SYSTEM.unmount("shader-cache");
        (void)FILE_SYSTEM.unmount("asset");
        (void)FILE_SYSTEM.unmount("library");
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 5 || std::string{argv[1]} != "compile") {
        std::cerr << "Usage: MiniShaderCompiler compile <shader.json> <pass-name> "
                     "<output-directory>\n";
        return 2;
    }

    const std::filesystem::path shaderPath = std::filesystem::absolute(argv[2]).lexically_normal();
    const std::filesystem::path shaderDirectory = shaderPath.parent_path();
    const std::filesystem::path assetRoot =
        shaderDirectory.filename() == "shaders" ? shaderDirectory.parent_path() : shaderDirectory;
    const std::filesystem::path output = std::filesystem::absolute(argv[4]).lexically_normal();
    const std::filesystem::path toolLibrary =
        output / ".shader-tool-library" / shaderPath.filename();
    if (!FILE_SYSTEM.mountDirectory("asset", assetRoot, false) ||
        !FILE_SYSTEM.mountDirectory("library", toolLibrary, false) || !ASSET_MANAGER.initialize()) {
        engine::Log::error(
            "MiniShaderCompiler", "Cannot initialize asset system: %s", assetRoot.string().c_str());
        return 1;
    }
    AssetRuntimeScope assetRuntime;

    const auto shaderVirtualPath = FILE_SYSTEM.toVirtualPath(shaderPath);
    if (!shaderVirtualPath || shaderVirtualPath->scheme() != "asset") {
        engine::Log::error("MiniShaderCompiler",
                           "Shader is outside the mounted asset root: %s",
                           shaderPath.string().c_str());
        return 1;
    }
    const std::shared_ptr<engine::ShaderAsset> shaderAsset =
        ASSET_MANAGER.loadAsset<engine::ShaderAsset>(*shaderVirtualPath);
    if (!shaderAsset || !FILE_SYSTEM.mountDirectory("shader-cache", output / "runtime", false) ||
        !FILE_SYSTEM.mountDirectory("shader-bin", output / "compiled", false))
        return 1;

    const engine::Shader shader{*shaderAsset};
    const engine::ShaderPass* pass = nullptr;
    for (const engine::SubShader& subShader : shader.subShaders()) {
        for (const engine::ShaderPass& candidate : subShader.passes()) {
            if (candidate.name() == argv[3]) {
                pass = &candidate;
                break;
            }
        }
        if (pass)
            break;
    }
    if (!pass) {
        engine::Log::error("MiniShaderCompiler", "Shader pass does not exist: %s", argv[3]);
        return 1;
    }

    engine::ShaderCompilePipelineConfig config;
    config.mode = engine::ShaderCompileMode::OfflineTool;
    config.preprocessorConfig.includeSearchPaths = {engine::VirtualPath{"asset://shaders/include"}};
    config.compilerOptions.compilerVersion = MINI_GLSLC_EXECUTABLE;
#if defined(MINI_SHADER_RELEASE)
    config.compilerOptions.optimization = engine::ShaderOptimization::Release;
#else
    config.compilerOptions.optimization = engine::ShaderOptimization::Debug;
#endif
    engine::ShaderCompilePipeline pipeline{std::move(config)};
    if (!pipeline.getOrCreate(shader, *pass))
        return 1;
    engine::Log::info("MiniShaderCompiler",
                      "Compiled packaged Shader: %s/%s",
                      shader.name().c_str(),
                      pass->name().c_str());
    return 0;
}
