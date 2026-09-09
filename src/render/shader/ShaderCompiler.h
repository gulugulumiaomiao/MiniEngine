#pragma once

#include "core/hash.h"
#include "render/shader/ShaderPreprocessor.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

enum class ShaderOptimization { Debug, Release };

struct ShaderCompilerOptions {
    ShaderOptimization optimization{ShaderOptimization::Debug};
    std::string compilerVersion;
    std::string arguments;
};

struct SpirvBinary {
    VirtualPath path;
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
    std::vector<std::byte> bytecode;
};

class ShaderCompiler final {
public:
    explicit ShaderCompiler(VirtualPath outputRoot);
    [[nodiscard]] std::shared_ptr<SpirvBinary> compile(const PreprocessedShader& shader,
                                                       const ShaderCompilerOptions& options);
    void invalidate(std::span<const VirtualPath> paths);
    void clear();

private:
    [[nodiscard]] static bool containsPath(std::span<const VirtualPath> paths,
                                           const VirtualPath& candidate);
#if defined(MINI_GLSLC_EXECUTABLE)
    [[nodiscard]] static std::string quotedPath(const std::filesystem::path& path);
#endif

    VirtualPath outputRoot_;
    std::unordered_map<Hash64, std::shared_ptr<SpirvBinary>> cache_;
};

} // namespace engine
