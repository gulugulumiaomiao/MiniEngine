#pragma once

#include "core/filesystem/FileDependencyGraph.h"
#include "render/shader/Shader.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

using ShaderHash = std::uint64_t;
using CompiledShaderId = ShaderHash;
using ShaderProgramId = ShaderHash;
using ShaderProgramLayoutId = ShaderHash;

enum class ShaderCompileMode { DevelopmentRuntime, OfflineTool, PackagedRuntime };
enum class ShaderOptimization { Debug, Release };

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
    ShaderHash sourceHash{};
};

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

struct CompiledShaderHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{};
    [[nodiscard]] explicit operator bool() const {
        return index != std::numeric_limits<std::uint32_t>::max();
    }
    bool operator==(const CompiledShaderHandle&) const = default;
};

struct CompiledShader {
    CompiledShaderId id{};
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
    std::vector<std::byte> bytecode;
    SpirvReflection reflection;
    VirtualPath binaryPath;
};

struct ShaderProgramLayout {
    ShaderProgramLayoutId id{};
    std::vector<ShaderDescriptorBinding> descriptors;
    std::vector<ShaderStageVariable> vertexInputs;
    std::vector<ShaderStageVariable> fragmentOutputs;
};

struct ShaderProgramHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t generation{};
    [[nodiscard]] explicit operator bool() const {
        return index != std::numeric_limits<std::uint32_t>::max();
    }
    bool operator==(const ShaderProgramHandle&) const = default;
};

struct ShaderProgram {
    ShaderProgramId id{};
    ShaderVariantKey variant;
    CompiledShaderHandle vertex;
    CompiledShaderHandle fragment;
    CompiledShaderId vertexId{};
    CompiledShaderId fragmentId{};
    ShaderProgramLayout layout;
};

struct ShaderCompilePipelineConfig {
    ShaderCompileMode mode{ShaderCompileMode::DevelopmentRuntime};
    ShaderPreprocessorConfig preprocessorConfig;
    ShaderCompilerOptions compilerOptions;
    VirtualPath intermediateRoot{"shader://runtime"};
    VirtualPath packagedRoot{"shader://compiled"};
};

[[nodiscard]] ShaderHash hashBytes(std::span<const std::byte> bytes,
                                   ShaderHash seed = 14695981039346656037ULL);
[[nodiscard]] ShaderHash hashString(std::string_view text,
                                    ShaderHash seed = 14695981039346656037ULL);

class ShaderPreprocessor final {
public:
    explicit ShaderPreprocessor(ShaderPreprocessorConfig config = {},
                                FileDependencyGraph& dependencies = FILE_DEPENDENCY_GRAPH);
    [[nodiscard]] std::shared_ptr<PreprocessedShader>
    process(const ShaderPreprocessRequest& request);
    void invalidate(std::span<const VirtualPath> paths);
    void clear();

private:
    ShaderPreprocessorConfig config_;
    FileDependencyGraph& dependencies_;
    bool configValid_{true};
    std::unordered_map<ShaderHash, std::shared_ptr<PreprocessedShader>> cache_;
};

class ShaderCompiler final {
public:
    ShaderCompiler(VirtualPath outputRoot,
                   FileDependencyGraph& dependencies = FILE_DEPENDENCY_GRAPH);
    [[nodiscard]] std::shared_ptr<SpirvBinary> compile(const PreprocessedShader& shader,
                                                       const ShaderCompilerOptions& options);
    void invalidate(std::span<const VirtualPath> paths);
    void clear();

private:
    VirtualPath outputRoot_;
    FileDependencyGraph& dependencies_;
    std::unordered_map<ShaderHash, std::shared_ptr<SpirvBinary>> cache_;
};

class CompiledShaderCache final {
public:
    [[nodiscard]] std::optional<CompiledShaderHandle> find(CompiledShaderId id) const;
    [[nodiscard]] CompiledShaderHandle insert(CompiledShader shader);
    [[nodiscard]] const CompiledShader& resolve(CompiledShaderHandle handle) const;
    [[nodiscard]] std::vector<CompiledShaderId> invalidatePaths(std::span<const VirtualPath> paths);
    void clear();

private:
    struct Slot {
        std::optional<CompiledShader> shader;
        std::uint32_t generation{1};
    };
    void removeId(CompiledShaderId id, std::vector<CompiledShaderId>& removed);
    std::unordered_map<CompiledShaderId, std::uint32_t> entries_;
    std::vector<Slot> slots_;
};

class ShaderProgramCache final {
public:
    [[nodiscard]] std::optional<ShaderProgramHandle> find(ShaderProgramId id) const;
    [[nodiscard]] ShaderProgramHandle insert(ShaderProgram program);
    [[nodiscard]] const ShaderProgram& resolve(ShaderProgramHandle handle) const;
    void invalidate(std::span<const CompiledShaderId> shaders);
    void clear();

private:
    struct Slot {
        std::optional<ShaderProgram> program;
        std::uint32_t generation{1};
    };
    std::unordered_map<ShaderProgramId, std::uint32_t> entries_;
    std::vector<Slot> slots_;
};

class ShaderCompilePipeline final {
public:
    explicit ShaderCompilePipeline(ShaderCompilePipelineConfig config = {},
                                   FileDependencyGraph& dependencies = FILE_DEPENDENCY_GRAPH);
    [[nodiscard]] ShaderProgramHandle
    getOrCreate(const Shader& shader, const ShaderPass& pass, const ShaderVariantKey& variant = {});
    [[nodiscard]] const ShaderProgram& resolve(ShaderProgramHandle handle) const;
    [[nodiscard]] const CompiledShader& resolve(CompiledShaderHandle handle) const;
    [[nodiscard]] CompiledShaderCache& compiledShaders() { return compiledShaders_; }
    [[nodiscard]] std::vector<CompiledShaderId> invalidate(const VirtualPath& changedFile);
    [[nodiscard]] std::vector<CompiledShaderId> invalidateChanged();
    void clear();

private:
    struct GeneratedSource {
        VirtualPath cachePath;
        std::shared_ptr<std::string> source;
    };
    struct CompiledStage {
        CompiledShaderHandle handle;
        VirtualPath binaryPath;
    };
    [[nodiscard]] std::optional<CompiledStage> compileStage(const Shader& shader,
                                                            const ShaderPass& pass,
                                                            ShaderStage stage,
                                                            const ShaderVariantKey& variant);
    [[nodiscard]] CompiledShaderHandle loadCompiledShader(const VirtualPath& binaryPath,
                                                          ShaderStage stage,
                                                          std::string_view entryPoint,
                                                          const ShaderVariantKey& variant);
    [[nodiscard]] static std::optional<ShaderProgramLayout>
    mergeLayout(const CompiledShader& vertex, const CompiledShader& fragment);
    void invalidateGeneratedSources(std::span<const VirtualPath> paths);
    ShaderCompilePipelineConfig config_;
    FileDependencyGraph& dependencies_;
    ShaderPreprocessor preprocessor_;
    ShaderCompiler compiler_;
    std::unordered_map<ShaderHash, GeneratedSource> generatedSources_;
    CompiledShaderCache compiledShaders_;
    ShaderProgramCache programs_;
};

} // namespace engine
