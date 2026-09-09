#pragma once

#include "core/base/Handle.h"
#include "core/base/KeyedHandleRegistry.h"
#include "core/hash.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompiler.h"
#include "render/shader/ShaderGenerator.h"
#include "render/shader/ShaderPreprocessor.h"

#include <cstddef>
#include <cstdint>
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

struct CompiledShaderHandleTag;
using CompiledShaderHandle = Handle<CompiledShaderHandleTag>;

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

struct ShaderProgramHandleTag;
using ShaderProgramHandle = Handle<ShaderProgramHandleTag>;

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
    class CompiledShaderCache final
        : public KeyedHandleRegistry<CompiledShader, CompiledShaderHandle, CompiledShaderId> {
    public:
        [[nodiscard]] std::optional<CompiledShaderHandle> findPath(ShaderHash pathKey) const;
        void rememberPath(ShaderHash pathKey, CompiledShaderHandle handle);
        [[nodiscard]] const CompiledShader& resolve(CompiledShaderHandle handle) const;
        [[nodiscard]] std::vector<CompiledShaderId>
        invalidatePaths(std::span<const VirtualPath> paths);
        void clear() override;

    private:
        [[nodiscard]] static bool containsPath(std::span<const VirtualPath> paths,
                                               const VirtualPath& candidate);
        void removeId(CompiledShaderId id, std::vector<CompiledShaderId>& removed);
        [[nodiscard]] CompiledShaderId keyOf(const CompiledShader& shader) const override {
            return shader.id;
        }

        std::unordered_map<ShaderHash, CompiledShaderHandle> pathEntries_;
    };
    class ShaderProgramCache final
        : public KeyedHandleRegistry<ShaderProgram, ShaderProgramHandle, ShaderProgramId> {
    public:
        [[nodiscard]] const ShaderProgram& resolve(ShaderProgramHandle handle) const;
        void invalidate(std::span<const CompiledShaderId> shaders);
        void clear() override;

    private:
        [[nodiscard]] ShaderProgramId keyOf(const ShaderProgram& program) const override {
            return program.id;
        }
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
    [[nodiscard]] static ShaderHash makeCompiledShaderPathKey(
        const VirtualPath& binaryPath,
        ShaderStage stage,
        std::string_view entryPoint,
        const ShaderVariantKey& variant);
    [[nodiscard]] static std::optional<SpirvReflection>
    reflectSpirv(std::span<const std::byte> bytecode, const VirtualPath& sourcePath);
    [[nodiscard]] static bool validateSpirvReflection(const Shader& shader,
                                                      const ShaderPass& pass,
                                                      const SpirvReflection& vertex,
                                                      const SpirvReflection& fragment,
                                                      const VirtualPath& vertexPath,
                                                      const VirtualPath& fragmentPath);
    [[nodiscard]] CompiledShaderHandle compileStage(const Shader& shader,
                                                    const ShaderPass& pass,
                                                    ShaderStage stage,
                                                    const ShaderVariantKey& variant);
    [[nodiscard]] CompiledShaderHandle loadCompiledShader(const VirtualPath& binaryPath,
                                                          ShaderStage stage,
                                                          std::string_view entryPoint,
                                                          const ShaderVariantKey& variant,
                                                          std::span<const std::byte> bytecode);
    [[nodiscard]] CompiledShaderHandle loadPackagedShader(const VirtualPath& binaryPath,
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
