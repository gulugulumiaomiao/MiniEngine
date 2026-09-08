#include "render/shader/ShaderCompiler.h"

#include "core/filesystem/FileSystem.h"
#include "core/hash.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <utility>

namespace engine {

bool ShaderCompiler::containsPath(std::span<const VirtualPath> paths,
                                  const VirtualPath& candidate) {
    return std::ranges::any_of(paths,
                               [&candidate](const VirtualPath& path) { return path == candidate; });
}

#if defined(MINI_GLSLC_EXECUTABLE)
std::string ShaderCompiler::quotedPath(const std::filesystem::path& path) {
    return '"' + path.string() + '"';
}
#endif

ShaderCompiler::ShaderCompiler(VirtualPath outputRoot, FileDependencyGraph& dependencies)
    : outputRoot_(std::move(outputRoot)), dependencies_(dependencies) {}

std::shared_ptr<SpirvBinary> ShaderCompiler::compile(const PreprocessedShader& shader,
                                                     const ShaderCompilerOptions& options) {
    Hash64 key = shader.sourceHash;
    hashAppend(key, options.optimization);
    key = hashString(options.compilerVersion, key);
    key = hashString(options.arguments, key);
    if (const auto found = cache_.find(key); found != cache_.end())
        return found->second;

    const std::string base =
        hashToHex(key) + (shader.stage == ShaderStage::Vertex ? ".vert" : ".frag");
    const VirtualPath sourcePath = outputRoot_.joined(base + ".glsl");
    const VirtualPath binaryPath = outputRoot_.joined(base + ".spv");
    if (!FILE_SYSTEM.isFile(binaryPath)) {
        if (!FILE_SYSTEM.createDirectories(outputRoot_) ||
            !FILE_SYSTEM.writeTextAtomic(sourcePath, shader.source)) {
            Log::error("ShaderCompiler",
                       "Cannot write preprocessed shader: %s",
                       sourcePath.string().c_str());
            return {};
        }
        const auto sourcePhysical = FILE_SYSTEM.resolvePhysicalPath(sourcePath);
        const auto binaryPhysical = FILE_SYSTEM.resolvePhysicalPath(binaryPath);
        if (!sourcePhysical || !binaryPhysical) {
            Log::error("ShaderCompiler", "Cannot resolve shader compiler paths");
            return {};
        }
#if !defined(MINI_GLSLC_EXECUTABLE)
        Log::error("ShaderCompiler", "glslc executable is not configured");
        return {};
#else
        std::string command = "\"" + quotedPath(MINI_GLSLC_EXECUTABLE);
        command += " --target-env=vulkan1.3 ";
        command += options.optimization == ShaderOptimization::Debug ? "-O0 -g " : "-O -g ";
        command +=
            shader.stage == ShaderStage::Vertex ? "-fshader-stage=vert " : "-fshader-stage=frag ";
        if (!options.arguments.empty())
            command += options.arguments + ' ';
        command += quotedPath(*sourcePhysical) + " -o " + quotedPath(*binaryPhysical) + '"';
        if (std::system(command.c_str()) != 0 || !FILE_SYSTEM.isFile(binaryPath)) {
            Log::error("ShaderCompiler",
                       "Shader compilation failed: %s",
                       shader.sourcePath.string().c_str());
            return {};
        }
#endif
    }
    const auto bytes = FILE_SYSTEM.readBinary(binaryPath);
    if (!bytes || bytes->empty())
        return {};
    auto result = std::make_shared<SpirvBinary>();
    result->path = binaryPath;
    result->stage = shader.stage;
    result->entryPoint = shader.entryPoint;
    result->bytecode = *bytes;
    std::vector<VirtualPath> inputs{shader.cachePath};
#if defined(MINI_GLSLC_EXECUTABLE)
    inputs.push_back(VirtualPath::fromNative(MINI_GLSLC_EXECUTABLE));
#endif
    dependencies_.replaceDependencies(binaryPath, inputs);
    cache_[key] = result;
    return result;
}

void ShaderCompiler::invalidate(std::span<const VirtualPath> paths) {
    std::erase_if(cache_, [&](const auto& entry) {
        if (!containsPath(paths, entry.second->path))
            return false;
        dependencies_.remove(entry.second->path);
        return true;
    });
}

void ShaderCompiler::clear() {
    for (const auto& [hash, shader] : cache_) {
        (void)hash;
        dependencies_.remove(shader->path);
    }
    cache_.clear();
}

} // namespace engine
