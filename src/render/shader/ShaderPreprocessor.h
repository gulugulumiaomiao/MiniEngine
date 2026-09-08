#pragma once

#include "core/filesystem/FileDependencyGraph.h"
#include "core/hash.h"
#include "render/shader/Shader.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine {

struct ShaderDefine {
    std::string name;
    std::string value{"1"};
};

struct ShaderStageSource {
    VirtualPath sourcePath;
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
    std::string source;
};

struct ShaderPreprocessorConfig {
    std::vector<VirtualPath> includeSearchPaths;
};

struct ShaderPreprocessRequest {
    ShaderStageSource source;
    std::vector<ShaderDefine> defines;
};

struct PreprocessedShader {
    VirtualPath sourcePath;
    VirtualPath cachePath;
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
    std::string source;
    std::vector<VirtualPath> dependencies;
    Hash64 sourceHash{};
};

class ShaderPreprocessor final {
public:
    explicit ShaderPreprocessor(ShaderPreprocessorConfig config = {},
                                FileDependencyGraph& dependencies = FILE_DEPENDENCY_GRAPH);
    [[nodiscard]] std::shared_ptr<PreprocessedShader>
    process(const ShaderPreprocessRequest& request);
    void invalidate(std::span<const VirtualPath> paths);
    void clear();

private:
    [[nodiscard]] static bool containsPath(std::span<const VirtualPath> paths,
                                           const VirtualPath& candidate);
    [[nodiscard]] static std::optional<VirtualPath>
    resolveInclude(const VirtualPath& includingFile,
                   std::string_view include,
                   bool local,
                   std::span<const VirtualPath> searchPaths,
                   std::vector<VirtualPath>& attempted);
    [[nodiscard]] static bool preprocessSource(const VirtualPath& path,
                                               std::string_view content,
                                               std::span<const VirtualPath> searchPaths,
                                               std::unordered_set<std::string>& visiting,
                                               PreprocessedShader& result);

    ShaderPreprocessorConfig config_;
    FileDependencyGraph& dependencies_;
    bool configValid_{true};
    std::unordered_map<Hash64, std::shared_ptr<PreprocessedShader>> cache_;
};

} // namespace engine
