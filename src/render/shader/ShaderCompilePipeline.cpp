#include "render/shader/ShaderCompilePipeline.h"

#include "core/filesystem/FileDependencyGraph.h"
#include "core/filesystem/FileSystem.h"
#include "core/hash.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <utility>

namespace engine {

bool ShaderCompilePipeline::containsPath(std::span<const VirtualPath> paths,
                                         const VirtualPath& candidate) {
    return std::ranges::any_of(paths,
                               [&candidate](const VirtualPath& path) { return path == candidate; });
}

ShaderProgramLayoutId ShaderCompilePipeline::makeLayoutId(const ShaderProgramLayout& layout) {
    Hash64 hash = hashString("ShaderProgramLayout");
    for (const ShaderDescriptorBinding& descriptor : layout.descriptors) {
        hashAppend(hash, descriptor.set);
        hashAppend(hash, descriptor.binding);
        hashAppend(hash, descriptor.type);
        hash = hashString(descriptor.name, hash);
        for (const ShaderUniformMember& member : descriptor.members) {
            hash = hashString(member.name, hash);
            hashAppend(hash, member.offset);
        }
    }
    for (const ShaderStageVariable& input : layout.vertexInputs) {
        hashAppend(hash, input.location);
        hashAppend(hash, input.type);
    }
    for (const ShaderStageVariable& output : layout.fragmentOutputs) {
        hashAppend(hash, output.location);
        hashAppend(hash, output.type);
    }
    return hash;
}

VirtualPath ShaderCompilePipeline::packagedBinaryPath(const ShaderCompilePipelineConfig& config,
                                                      const Shader& shader,
                                                      const ShaderPass& pass,
                                                      ShaderStage stage,
                                                      const ShaderVariantKey& variant) {
    Hash64 hash = hashString(shader.assetPath().string());
    hash = hashString(pass.name(), hash);
    hashAppend(hash, pass.type());
    hashAppend(hash, variant.keywordBits);
    hashAppend(hash, variant.meshFeatureBits);
    hashAppend(hash, variant.platformFeatureBits);
    const std::string suffix = stage == ShaderStage::Vertex ? ".vert.spv" : ".frag.spv";
    return config.packagedRoot.joined(hashToHex(hash) + suffix);
}

CompiledShaderId ShaderCompilePipeline::makeCompiledShaderId(std::span<const std::byte> bytecode,
                                                             ShaderStage stage,
                                                             std::string_view entryPoint,
                                                             const ShaderVariantKey& variant) {
    Hash64 hash = hashBytes(bytecode);
    hashAppend(hash, stage);
    hash = hashString(entryPoint, hash);
    hashAppend(hash, variant.keywordBits);
    hashAppend(hash, variant.meshFeatureBits);
    hashAppend(hash, variant.platformFeatureBits);
    return hash;
}

std::optional<CompiledShaderHandle>
ShaderCompilePipeline::CompiledShaderCache::find(CompiledShaderId id) const {
    if (const auto found = entries_.find(id); found != entries_.end() && pool_.find(found->second))
        return found->second;
    return std::nullopt;
}

bool ShaderCompilePipeline::CompiledShaderCache::containsPath(std::span<const VirtualPath> paths,
                                                              const VirtualPath& candidate) {
    return std::ranges::any_of(paths,
                               [&candidate](const VirtualPath& path) { return path == candidate; });
}

CompiledShaderHandle ShaderCompilePipeline::CompiledShaderCache::insert(CompiledShader shader) {
    if (const auto existing = find(shader.id))
        return *existing;
    const CompiledShaderId id = shader.id;
    const CompiledShaderHandle handle = pool_.insert(std::move(shader));
    entries_.emplace(id, handle);
    return handle;
}

const CompiledShader&
ShaderCompilePipeline::CompiledShaderCache::resolve(CompiledShaderHandle handle) const {
    const CompiledShader* shader = pool_.find(handle);
    if (!shader)
        Log::fatal("CompiledShaderCache", "Invalid or stale compiled shader handle");
    return *shader;
}

void ShaderCompilePipeline::CompiledShaderCache::removeId(CompiledShaderId id,
                                                          std::vector<CompiledShaderId>& removed) {
    const auto found = entries_.find(id);
    if (found == entries_.end())
        return;
    (void)pool_.release(found->second);
    entries_.erase(found);
    removed.push_back(id);
}

std::vector<CompiledShaderId>
ShaderCompilePipeline::CompiledShaderCache::invalidatePaths(std::span<const VirtualPath> paths) {
    std::vector<CompiledShaderId> removed;
    std::vector<CompiledShaderId> ids;
    for (const auto& [id, handle] : entries_) {
        const CompiledShader* shader = pool_.find(handle);
        if (shader && containsPath(paths, shader->binaryPath))
            ids.push_back(id);
    }
    for (CompiledShaderId id : ids)
        removeId(id, removed);
    return removed;
}

void ShaderCompilePipeline::CompiledShaderCache::clear() {
    entries_.clear();
    pool_.clear();
}

std::optional<ShaderProgramHandle>
ShaderCompilePipeline::ShaderProgramCache::find(ShaderProgramId id) const {
    if (const auto found = entries_.find(id); found != entries_.end() && pool_.find(found->second))
        return found->second;
    return std::nullopt;
}

ShaderProgramHandle ShaderCompilePipeline::ShaderProgramCache::insert(ShaderProgram program) {
    if (const auto existing = find(program.id))
        return *existing;
    const ShaderProgramId id = program.id;
    const ShaderProgramHandle handle = pool_.insert(std::move(program));
    entries_.emplace(id, handle);
    return handle;
}

const ShaderProgram&
ShaderCompilePipeline::ShaderProgramCache::resolve(ShaderProgramHandle handle) const {
    const ShaderProgram* program = pool_.find(handle);
    if (!program)
        Log::fatal("ShaderProgramCache", "Invalid or stale shader program handle");
    return *program;
}

void ShaderCompilePipeline::ShaderProgramCache::invalidate(
    std::span<const CompiledShaderId> shaders) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        const ShaderProgram* program = pool_.find(entry->second);
        const bool affected = program && std::ranges::any_of(shaders, [program](auto id) {
                                  return program->vertexId == id || program->fragmentId == id;
                              });
        if (affected) {
            (void)pool_.release(entry->second);
            entry = entries_.erase(entry);
        } else {
            ++entry;
        }
    }
}

void ShaderCompilePipeline::ShaderProgramCache::clear() {
    entries_.clear();
    pool_.clear();
}

ShaderCompilePipeline::ShaderCompilePipeline(ShaderCompilePipelineConfig config)
    : config_(std::move(config)), preprocessor_(config_.preprocessorConfig),
      compiler_(config_.intermediateRoot) {}

CompiledShaderHandle ShaderCompilePipeline::loadCompiledShader(const VirtualPath& binaryPath,
                                                               ShaderStage stage,
                                                               std::string_view entryPoint,
                                                               const ShaderVariantKey& variant) {
    const auto bytes = FILE_SYSTEM.readBinary(binaryPath);
    if (!bytes || bytes->empty() || bytes->size() % sizeof(std::uint32_t) != 0) {
        Log::error(
            "ShaderCompilePipeline", "Invalid or missing SPIR-V: %s", binaryPath.string().c_str());
        return {};
    }
    const CompiledShaderId id = makeCompiledShaderId(*bytes, stage, entryPoint, variant);
    if (const auto cached = compiledShadersCache_.find(id))
        return *cached;
    const auto reflection = reflectSpirv(binaryPath);
    if (!reflection || reflection->stage != stage)
        return {};
    return compiledShadersCache_.insert(
        {id, stage, std::string{entryPoint}, *bytes, *reflection, binaryPath});
}

CompiledShaderHandle ShaderCompilePipeline::compileStage(const Shader& shader,
                                                         const ShaderPass& pass,
                                                         ShaderStage stage,
                                                         const ShaderVariantKey& variant) {
    const VirtualPath packaged = packagedBinaryPath(config_, shader, pass, stage, variant);
    if (config_.mode == ShaderCompileMode::PackagedRuntime) {
        if (!FILE_SYSTEM.isFile(packaged)) {
            Log::error("ShaderCompilePipeline",
                       "Packaged SPIR-V is missing: %s/%s (%s): %s",
                       shader.name().c_str(),
                       pass.name().c_str(),
                       stage == ShaderStage::Vertex ? "vertex" : "fragment",
                       packaged.string().c_str());
            return {};
        }
        return loadCompiledShader(packaged, stage, "main", variant);
    }

    const VirtualPath sourcePath =
        stage == ShaderStage::Vertex ? pass.program().vertexSource : pass.program().fragmentSource;
    const auto userSource = FILE_SYSTEM.readText(sourcePath);
    if (!userSource)
        return {};

    ShaderHash generatedKey = hashString(shader.assetPath().string());
    generatedKey = hashString(pass.name(), generatedKey);
    generatedKey = hashString(sourcePath.string(), generatedKey);
    generatedKey = hashString(*userSource, generatedKey);
    hashAppend(generatedKey, shader.revision());
    hashAppend(generatedKey, stage);
    auto generatedEntry = generatedSources_.find(generatedKey);
    if (generatedEntry == generatedSources_.end()) {
        auto generated = generator_.generateStage(shader, pass, stage, *userSource);
        if (!generated)
            return {};
        GeneratedSource entry;
        entry.cachePath = VirtualPath{"shader-generated://" + hashToHex(generatedKey) + ".glsl"};
        entry.source = std::move(*generated);
        const std::array dependencies{shader.assetPath(), sourcePath};
        FILE_DEPENDENCY_GRAPH.replaceDependencies(entry.cachePath, dependencies);
        generatedEntry = generatedSources_.emplace(generatedKey, std::move(entry)).first;
    }

    ShaderPreprocessRequest request;
    request.sourcePath = sourcePath;
    request.stage = stage;
    request.source = generatedEntry->second.source;
    const std::vector<std::string>& keywords = pass.keywordSchema().keywords();
    for (std::size_t bit = 0; bit < keywords.size(); ++bit) {
        if ((variant.keywordBits & (std::uint64_t{1} << bit)) != 0)
            request.defines.push_back({keywords[bit], "1"});
    }
    request.defines.push_back({"MINI_MESH_FEATURE_BITS", std::to_string(variant.meshFeatureBits)});
    request.defines.push_back(
        {"MINI_PLATFORM_FEATURE_BITS", std::to_string(variant.platformFeatureBits)});
    const auto processed = preprocessor_.process(request);
    if (!processed)
        return {};
    const auto spirv = compiler_.compile(*processed, config_.compilerOptions);
    if (!spirv)
        return {};

    VirtualPath binaryPath = spirv->path;
    if (config_.mode == ShaderCompileMode::OfflineTool) {
        if (!FILE_SYSTEM.createDirectories(config_.packagedRoot) ||
            !FILE_SYSTEM.writeBinaryAtomic(packaged, spirv->bytecode)) {
            Log::error("ShaderCompilePipeline",
                       "Cannot write packaged SPIR-V: %s",
                       packaged.string().c_str());
            return {};
        }
        const std::array dependency{spirv->path};
        FILE_DEPENDENCY_GRAPH.replaceDependencies(packaged, dependency);
        binaryPath = packaged;
    }
    return loadCompiledShader(binaryPath, stage, processed->entryPoint, variant);
}

std::optional<ShaderProgramLayout>
ShaderCompilePipeline::mergeLayout(const CompiledShader& vertex, const CompiledShader& fragment) {
    ShaderProgramLayout layout;
    layout.vertexInputs = vertex.reflection.inputs;
    layout.fragmentOutputs = fragment.reflection.outputs;
    layout.descriptors = vertex.reflection.descriptors;
    for (const ShaderDescriptorBinding& descriptor : fragment.reflection.descriptors) {
        const auto existing = std::ranges::find_if(
            layout.descriptors, [&descriptor](const ShaderDescriptorBinding& value) {
                return value.set == descriptor.set && value.binding == descriptor.binding;
            });
        if (existing == layout.descriptors.end())
            layout.descriptors.push_back(descriptor);
        else if (existing->type != descriptor.type)
            return std::nullopt;
        else if (existing->members.empty())
            existing->members = descriptor.members;
    }
    std::ranges::sort(layout.descriptors, [](const auto& left, const auto& right) {
        return std::tie(left.set, left.binding) < std::tie(right.set, right.binding);
    });
    layout.id = makeLayoutId(layout);
    return layout;
}

ShaderProgramHandle ShaderCompilePipeline::getOrCreate(const Shader& shader,
                                                       const ShaderPass& pass,
                                                       const ShaderVariantKey& variant) {
    const CompiledShaderHandle vertexHandle =
        compileStage(shader, pass, ShaderStage::Vertex, variant);
    if (!vertexHandle)
        return {};
    const CompiledShaderHandle fragmentHandle =
        compileStage(shader, pass, ShaderStage::Fragment, variant);
    if (!fragmentHandle)
        return {};
    const CompiledShader& vertex = compiledShadersCache_.resolve(vertexHandle);
    const CompiledShader& fragment = compiledShadersCache_.resolve(fragmentHandle);
    ShaderProgramId id = vertex.id;
    hashAppend(id, fragment.id);
    hashAppend(id, variant.keywordBits);
    hashAppend(id, variant.meshFeatureBits);
    hashAppend(id, variant.platformFeatureBits);
    if (const auto cached = programsCache_.find(id))
        return *cached;
    if (!validateSpirvReflection(shader, pass, vertex.binaryPath, fragment.binaryPath)) {
        return {};
    }
    auto layout = mergeLayout(vertex, fragment);
    if (!layout)
        return {};
    return programsCache_.insert({id,
                                  variant,
                                  vertexHandle,
                                  fragmentHandle,
                                  vertex.id,
                                  fragment.id,
                                  std::move(*layout)});
}

const ShaderProgram& ShaderCompilePipeline::resolve(ShaderProgramHandle handle) const {
    return programsCache_.resolve(handle);
}

const CompiledShader& ShaderCompilePipeline::resolve(CompiledShaderHandle handle) const {
    return compiledShadersCache_.resolve(handle);
}

std::vector<CompiledShaderId> ShaderCompilePipeline::invalidate(const VirtualPath& changedFile) {
    std::vector<VirtualPath> affected =
        FILE_DEPENDENCY_GRAPH.transitiveDependentsOf(changedFile);
    affected.push_back(changedFile);
    invalidateGeneratedSources(affected);
    preprocessor_.invalidate(affected);
    compiler_.invalidate(affected);
    std::vector<CompiledShaderId> removed = compiledShadersCache_.invalidatePaths(affected);
    programsCache_.invalidate(removed);
    return removed;
}

void ShaderCompilePipeline::invalidateGeneratedSources(std::span<const VirtualPath> paths) {
    std::erase_if(generatedSources_, [&](const auto& entry) {
        if (!containsPath(paths, entry.second.cachePath))
            return false;
        FILE_DEPENDENCY_GRAPH.remove(entry.second.cachePath);
        return true;
    });
}

std::vector<CompiledShaderId> ShaderCompilePipeline::invalidateChanged() {
    std::vector<CompiledShaderId> result;
    for (const VirtualPath& path : FILE_DEPENDENCY_GRAPH.consumeChangedFiles()) {
        std::vector<CompiledShaderId> removed = invalidate(path);
        result.insert(result.end(), removed.begin(), removed.end());
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void ShaderCompilePipeline::clear() {
    for (const auto& [hash, source] : generatedSources_) {
        (void)hash;
        FILE_DEPENDENCY_GRAPH.remove(source.cachePath);
    }
    generatedSources_.clear();
    programsCache_.clear();
    compiledShadersCache_.clear();
    compiler_.clear();
    preprocessor_.clear();
}

} // namespace engine
