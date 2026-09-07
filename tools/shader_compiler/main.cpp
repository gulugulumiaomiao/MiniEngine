#include "asset/manager/AssetManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompiler.h"
#include "render/shader/ShaderGenerator.h"
#include "core/logging/Log.h"
#include "core/filesystem/FileSystem.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

namespace {

class AssetRuntimeScope final {
public:
    ~AssetRuntimeScope() {
        ASSET_MANAGER.shutdown();
        (void)FILE_SYSTEM.unmount("asset");
        (void)FILE_SYSTEM.unmount("library");
    }
};

bool writeTextFile(const std::filesystem::path& path, const std::string& contents) {
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            engine::Log::error("MiniShaderCompiler",
                               "Cannot create output directory: %s",
                               path.parent_path().string().c_str());
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        engine::Log::error(
            "MiniShaderCompiler", "Cannot open generated Shader output: %s", path.string().c_str());
        return false;
    }
    output << contents;
    if (!output) {
        engine::Log::error("MiniShaderCompiler",
                           "Failed to write generated Shader output: %s",
                           path.string().c_str());
        return false;
    }
    return true;
}

const engine::ShaderPassDesc* findPass(const engine::ShaderAsset& shader, const std::string& name) {
    for (const engine::SubShaderDesc& subShader : shader.subShaders) {
        for (const engine::ShaderPassAsset& passAsset : subShader.passes) {
            const engine::ShaderPassDesc& pass = passAsset.pass;
            if (pass.name == name) {
                return &pass;
            }
        }
    }
    engine::Log::error("MiniShaderCompiler", "Shader pass does not exist: %s", name.c_str());
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    const bool propertyDeclarationsMode = argc == 4 && std::string{argv[1]} == "properties";
    const bool stageSourcesMode = argc == 6 && std::string{argv[1]} == "stages";
    const bool reflectionMode = argc == 6 && std::string{argv[1]} == "reflect";
    if (!propertyDeclarationsMode && !stageSourcesMode && !reflectionMode) {
        std::cerr << "Usage:\n"
                     "  MiniShaderCompiler properties <shader.json> <output.glsl>\n"
                     "  MiniShaderCompiler stages <shader.json> <pass-name> "
                     "<vertex-output.glsl> <fragment-output.glsl>\n"
                     "  MiniShaderCompiler reflect <shader.json> <pass-name> "
                     "<vertex.spv> <fragment.spv>\n";
        return 2;
    }
    const std::filesystem::path shaderPath = std::filesystem::absolute(argv[2]).lexically_normal();
    const std::filesystem::path shaderDirectory = shaderPath.parent_path();
    const std::filesystem::path assetRoot =
        shaderDirectory.filename() == "shaders" ? shaderDirectory.parent_path() : shaderDirectory;
    if (!FILE_SYSTEM.mountDirectory("asset", assetRoot, false) ||
        !FILE_SYSTEM.mountDirectory("library", assetRoot.parent_path() / "library", false) ||
        !ASSET_MANAGER.initialize()) {
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
    if (!shaderAsset)
        return 1;
    const engine::ShaderAsset& shader = *shaderAsset;
    const engine::UniformBlockLayout layout = engine::buildUniformBlockLayout(shader.properties);

    if (propertyDeclarationsMode) {
        const auto generated =
            engine::shader_compiler::generateMaterialDeclarations(shader, layout);
        if (!generated || !writeTextFile(argv[3], generated->glsl))
            return 1;
    } else if (stageSourcesMode) {
        const engine::ShaderPassDesc* pass = findPass(shader, argv[3]);
        if (!pass)
            return 1;
        if (!pass->program.hasSourceProgram()) {
            engine::Log::error(
                "MiniShaderCompiler", "Shader pass has no source program: %s", pass->name.c_str());
            return 1;
        }
        engine::ShaderPreprocessor preprocessor;
        engine::ShaderCompileRequest vertexRequest;
        vertexRequest.source = pass->program.vertexSource;
        vertexRequest.stage = engine::ShaderStage::Vertex;
        vertexRequest.shaderAsset = &shader;
        vertexRequest.shaderPass = pass;
        engine::ShaderCompileRequest fragmentRequest;
        fragmentRequest.source = pass->program.fragmentSource;
        fragmentRequest.stage = engine::ShaderStage::Fragment;
        fragmentRequest.shaderAsset = &shader;
        fragmentRequest.shaderPass = pass;
        const auto vertex = preprocessor.process(vertexRequest);
        const auto fragment = preprocessor.process(fragmentRequest);
        if (!vertex || !fragment || !writeTextFile(argv[4], vertex->source) ||
            !writeTextFile(argv[5], fragment->source))
            return 1;
    } else {
        const engine::ShaderPassDesc* pass = findPass(shader, argv[3]);
        if (!pass || !engine::validateSpirvReflection(
                         shader,
                         *pass,
                         engine::VirtualPath::fromNative(std::filesystem::absolute(argv[4])),
                         engine::VirtualPath::fromNative(std::filesystem::absolute(argv[5]))))
            return 1;
        engine::Log::info("MiniShaderCompiler",
                          "SPIR-V reflection validated: %s/%s",
                          shader.name.c_str(),
                          pass->name.c_str());
    }
}
