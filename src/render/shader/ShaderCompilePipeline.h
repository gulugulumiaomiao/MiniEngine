#pragma once

#include "core/base/HandlePool.h"
#include "core/hash.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompiler.h"
#include "render/shader/ShaderGenerator.h"
#include "render/shader/ShaderPreprocessor.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

using ShaderHash = Hash64;
using CompiledShaderId = ShaderHash;
using ShaderProgramId = ShaderHash;
using ShaderProgramLayoutId = ShaderHash;

enum class ShaderCompileMode { DevelopmentRuntime, OfflineTool, PackagedRuntime };

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

class ShaderCompilePipeline final {
public:
    explicit ShaderCompilePipeline(ShaderCompilePipelineConfig config = {});
    [[nodiscard]] ShaderProgramHandle
    getOrCreate(const Shader& shader, const ShaderPass& pass, const ShaderVariantKey& variant = {});
    [[nodiscard]] const ShaderProgram& resolve(ShaderProgramHandle handle) const;
    [[nodiscard]] const CompiledShader& resolve(CompiledShaderHandle handle) const;
    [[nodiscard]] std::vector<CompiledShaderId> invalidate(const VirtualPath& changedFile);
    [[nodiscard]] std::vector<CompiledShaderId> invalidateChanged();
    void clear();

private:
    struct GeneratedSource {
        VirtualPath cachePath;
        std::string source;
    };
    struct CompiledStage {
        CompiledShaderHandle handle;
        VirtualPath binaryPath;
    };
    class CompiledShaderCache final {
    public:
        [[nodiscard]] std::optional<CompiledShaderHandle> find(CompiledShaderId id) const;
        [[nodiscard]] CompiledShaderHandle insert(CompiledShader shader);
        [[nodiscard]] const CompiledShader& resolve(CompiledShaderHandle handle) const;
        [[nodiscard]] std::vector<CompiledShaderId>
        invalidatePaths(std::span<const VirtualPath> paths);
        void clear();

    private:
        [[nodiscard]] static bool containsPath(std::span<const VirtualPath> paths,
                                               const VirtualPath& candidate);
        void removeId(CompiledShaderId id, std::vector<CompiledShaderId>& removed);
        std::unordered_map<CompiledShaderId, CompiledShaderHandle> entries_;
        HandlePool<CompiledShader, CompiledShaderHandle> pool_;
    };
    class ShaderProgramCache final {
    public:
        [[nodiscard]] std::optional<ShaderProgramHandle> find(ShaderProgramId id) const;
        [[nodiscard]] ShaderProgramHandle insert(ShaderProgram program);
        [[nodiscard]] const ShaderProgram& resolve(ShaderProgramHandle handle) const;
        void invalidate(std::span<const CompiledShaderId> shaders);
        void clear();

    private:
        std::unordered_map<ShaderProgramId, ShaderProgramHandle> entries_;
        HandlePool<ShaderProgram, ShaderProgramHandle> pool_;
    };

    [[nodiscard]] static bool containsPath(std::span<const VirtualPath> paths,
                                           const VirtualPath& candidate);
    [[nodiscard]] static ShaderProgramLayoutId makeLayoutId(const ShaderProgramLayout& layout);
    [[nodiscard]] static VirtualPath packagedBinaryPath(const ShaderCompilePipelineConfig& config,
                                                        const Shader& shader,
                                                        const ShaderPass& pass,
                                                        ShaderStage stage,
                                                        const ShaderVariantKey& variant);
    [[nodiscard]] static CompiledShaderId makeCompiledShaderId(std::span<const std::byte> bytecode,
                                                               ShaderStage stage,
                                                               std::string_view entryPoint,
                                                               const ShaderVariantKey& variant);
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
    ShaderGenerator generator_;
    ShaderPreprocessor preprocessor_;
    ShaderCompiler compiler_;
    std::unordered_map<ShaderHash, GeneratedSource> generatedSources_;
    CompiledShaderCache compiledShadersCache_;
    ShaderProgramCache programsCache_;
};

} // namespace engine
