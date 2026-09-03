#pragma once

#include "renderer/Shader.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine {

using ShaderHash = std::uint64_t;
using CompiledShaderId = ShaderHash;
using ShaderProgramId = ShaderHash;
using ShaderProgramLayoutId = ShaderHash;

enum class ShaderBinaryFormat { Spirv };
enum class ShaderTargetApi { Vulkan };

struct ShaderDefine {
    std::string name;
    std::string value{"1"};
};

struct ShaderCompileRequest {
    VirtualPath source;
    std::string entryPoint{"main"};
    ShaderStage stage{ShaderStage::Vertex};
    std::vector<ShaderDefine> defines;
    std::vector<VirtualPath> includePaths;
    const ShaderAsset* shaderAsset{};
    const ShaderPassDesc* shaderPass{};
    ShaderTargetApi target{ShaderTargetApi::Vulkan};
    std::string compilerVersion;
    std::string options;
};

struct PreprocessedShader {
    std::string source;
    std::vector<VirtualPath> dependencies;
    ShaderHash sourceHash{};
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
    ShaderBinaryFormat format{ShaderBinaryFormat::Spirv};
    std::string entryPoint{"main"};
    std::vector<std::byte> bytecode;
    SpirvReflection reflection;
    std::vector<VirtualPath> dependencies;
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

struct ShaderCookedVariant {
    ShaderVariantKey key;
    CompiledShaderId vertex{};
    CompiledShaderId fragment{};
    ShaderProgramLayoutId layout{};
};

struct ShaderCookedPass {
    std::string name;
    ShaderPassType type{ShaderPassType::Forward};
    RenderStateDesc renderState;
    std::vector<ShaderCookedVariant> variants;
};

struct ShaderCookedAsset {
    std::string name;
    std::vector<ShaderPropertyDesc> properties;
    UniformBlockLayout materialLayout;
    std::vector<ShaderCookedPass> passes;
};

[[nodiscard]] ShaderHash hashBytes(std::span<const std::byte> bytes,
                                   ShaderHash seed = 14695981039346656037ULL);
[[nodiscard]] ShaderHash hashString(
    std::string_view text, ShaderHash seed = 14695981039346656037ULL);

class ShaderPreprocessor final {
public:
    [[nodiscard]] std::shared_ptr<PreprocessedShader> process(
        const ShaderCompileRequest& request) const;
};

class ShaderDependencyGraph final {
public:
    void track(CompiledShaderId shader,
               std::span<const VirtualPath> dependencies);
    [[nodiscard]] std::vector<CompiledShaderId> affectedBy(
        const VirtualPath& dependency) const;
    void remove(CompiledShaderId shader);
    void clear();

private:
    std::unordered_map<std::string, std::unordered_set<CompiledShaderId>> edges_;
};

class CompiledShaderCache final {
public:
    [[nodiscard]] CompiledShaderHandle getOrLoad(
        const VirtualPath& binaryPath, ShaderStage stage,
        std::string_view entryPoint, const ShaderVariantKey& variant = {});
    [[nodiscard]] const CompiledShader& resolve(CompiledShaderHandle handle) const;
    [[nodiscard]] std::vector<CompiledShaderId> invalidateDependency(
        const VirtualPath& dependency);
    [[nodiscard]] std::vector<CompiledShaderId> invalidateChanged();
    void clear();

private:
    struct Slot {
        std::optional<CompiledShader> shader;
        std::filesystem::file_time_type timestamp{};
        std::uint32_t generation{1};
    };

    [[nodiscard]] static CompiledShaderId makeId(
        std::span<const std::byte> bytecode, ShaderStage stage,
        std::string_view entryPoint, const ShaderVariantKey& variant);
    void removeId(CompiledShaderId id, std::vector<CompiledShaderId>& removed);

    std::unordered_map<CompiledShaderId, std::uint32_t> entries_;
    std::vector<Slot> slots_;
    ShaderDependencyGraph dependencies_;
};

class ShaderProgramCache final {
public:
    explicit ShaderProgramCache(CompiledShaderCache& shaders);

    [[nodiscard]] ShaderProgramHandle getOrCreate(
        const Shader& shader, const ShaderPass& pass,
        const ShaderVariantKey& variant = {});
    [[nodiscard]] const ShaderProgram& resolve(ShaderProgramHandle handle) const;
    void invalidate(std::span<const CompiledShaderId> shaders);
    void clear();

private:
    struct Slot {
        std::optional<ShaderProgram> program;
        std::uint32_t generation{1};
    };

    [[nodiscard]] static std::optional<ShaderProgramLayout> mergeLayout(
        const CompiledShader& vertex, const CompiledShader& fragment);
    [[nodiscard]] CompiledShaderHandle compileStage(
        const Shader& shader, const ShaderPass& pass, ShaderStage stage,
        const ShaderVariantKey& variant, VirtualPath& binaryPath);

    CompiledShaderCache& shaders_;
    std::unordered_map<ShaderProgramId, std::uint32_t> entries_;
    std::vector<Slot> slots_;
};

[[nodiscard]] ShaderCookedAsset buildCookedShaderAsset(
    const ShaderAsset& asset,
    std::span<const std::pair<const ShaderPass*, ShaderProgram>> programs);

} // namespace engine
