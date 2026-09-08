#include "render/shader/ShaderCompiler.h"

#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/shader/ShaderGenerator.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace engine {
namespace {

template <typename Value> void hashValue(ShaderHash& hash, const Value& value) {
    hash = hashBytes({reinterpret_cast<const std::byte*>(&value), sizeof(Value)}, hash);
}

std::string hashName(ShaderHash hash) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

bool containsPath(std::span<const VirtualPath> paths, const VirtualPath& candidate) {
    return std::ranges::any_of(paths,
                               [&candidate](const VirtualPath& path) { return path == candidate; });
}

std::optional<VirtualPath> resolveInclude(const VirtualPath& includingFile,
                                          std::string_view include,
                                          bool local,
                                          std::span<const VirtualPath> searchPaths,
                                          std::vector<VirtualPath>& attempted) {
    if (include.find("://") != std::string_view::npos) {
        VirtualPath path{include};
        attempted.push_back(path);
        return path.valid() && FILE_SYSTEM.isFile(path) ? std::optional<VirtualPath>{path}
                                                        : std::nullopt;
    }
    if (local) {
        const VirtualPath path = includingFile.parent().joined(include);
        attempted.push_back(path);
        if (path.valid() && FILE_SYSTEM.isFile(path))
            return path;
    }
    for (const VirtualPath& root : searchPaths) {
        const VirtualPath path = root.joined(include);
        attempted.push_back(path);
        if (path.valid() && FILE_SYSTEM.isFile(path))
            return path;
    }
    return std::nullopt;
}

bool preprocessSource(const VirtualPath& path,
                      std::string_view content,
                      std::span<const VirtualPath> searchPaths,
                      std::unordered_set<std::string>& visiting,
                      PreprocessedShader& result) {
    const std::string key = path.string();
    if (!visiting.insert(key).second) {
        Log::error("ShaderPreprocessor", "Cyclic include dependency: %s", key.c_str());
        return false;
    }
    if (!containsPath(result.dependencies, path))
        result.dependencies.push_back(path);

    std::istringstream lines{std::string{content}};
    std::string line;
    std::uint32_t lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 8, "#include") != 0) {
            result.source += line + '\n';
            continue;
        }
        const std::size_t delimiter = line.find_first_of("\"<", first + 8);
        if (delimiter == std::string::npos) {
            Log::error("ShaderPreprocessor", "Malformed include in: %s", key.c_str());
            return false;
        }
        const bool local = line[delimiter] == '"';
        const char closing = local ? '"' : '>';
        const std::size_t end = line.find(closing, delimiter + 1);
        if (end == std::string::npos) {
            Log::error("ShaderPreprocessor", "Malformed include in: %s", key.c_str());
            return false;
        }
        const std::string include = line.substr(delimiter + 1, end - delimiter - 1);
        std::vector<VirtualPath> attempted;
        const auto resolved = resolveInclude(path, include, local, searchPaths, attempted);
        if (!resolved) {
            std::string searched;
            for (const VirtualPath& candidate : attempted)
                searched += "\n  " + candidate.string();
            Log::error("ShaderPreprocessor",
                       "Cannot resolve include %s from %s. Searched:%s",
                       include.c_str(),
                       key.c_str(),
                       searched.c_str());
            return false;
        }
        const auto included = FILE_SYSTEM.readText(*resolved);
        if (!included) {
            Log::error("ShaderPreprocessor", "Cannot read include: %s", resolved->string().c_str());
            return false;
        }
        result.source += "#line 1 \"" + resolved->string() + "\"\n";
        if (!preprocessSource(*resolved, *included, searchPaths, visiting, result))
            return false;
        result.source += "#line " + std::to_string(lineNumber + 1) + " \"" + path.string() + "\"\n";
    }
    visiting.erase(key);
    return true;
}

ShaderHash hashReflection(const ShaderProgramLayout& layout) {
    ShaderHash hash = hashString("ShaderProgramLayout");
    for (const ShaderDescriptorBinding& descriptor : layout.descriptors) {
        hashValue(hash, descriptor.set);
        hashValue(hash, descriptor.binding);
        hashValue(hash, descriptor.type);
        hash = hashString(descriptor.name, hash);
        for (const ShaderUniformMember& member : descriptor.members) {
            hash = hashString(member.name, hash);
            hashValue(hash, member.offset);
        }
    }
    for (const ShaderStageVariable& input : layout.vertexInputs) {
        hashValue(hash, input.location);
        hashValue(hash, input.type);
    }
    for (const ShaderStageVariable& output : layout.fragmentOutputs) {
        hashValue(hash, output.location);
        hashValue(hash, output.type);
    }
    return hash;
}

VirtualPath packagedBinaryPath(const ShaderCompilePipelineConfig& config,
                               const Shader& shader,
                               const ShaderPass& pass,
                               ShaderStage stage,
                               const ShaderVariantKey& variant) {
    ShaderHash hash = hashString(shader.assetPath().string());
    hash = hashString(pass.name(), hash);
    hashValue(hash, pass.type());
    hashValue(hash, variant.keywordBits);
    hashValue(hash, variant.meshFeatureBits);
    hashValue(hash, variant.platformFeatureBits);
    const std::string suffix = stage == ShaderStage::Vertex ? ".vert.spv" : ".frag.spv";
    return config.packagedRoot.joined(hashName(hash) + suffix);
}

CompiledShaderId makeCompiledShaderId(std::span<const std::byte> bytecode,
                                      ShaderStage stage,
                                      std::string_view entryPoint,
                                      const ShaderVariantKey& variant) {
    ShaderHash hash = hashBytes(bytecode);
    hashValue(hash, stage);
    hash = hashString(entryPoint, hash);
    hashValue(hash, variant.keywordBits);
    hashValue(hash, variant.meshFeatureBits);
    hashValue(hash, variant.platformFeatureBits);
    return hash;
}

#if defined(MINI_GLSLC_EXECUTABLE)
std::string quotedPath(const std::filesystem::path& path) {
    return '"' + path.string() + '"';
}
#endif

} // namespace

ShaderHash hashBytes(std::span<const std::byte> bytes, ShaderHash seed) {
    constexpr ShaderHash prime = 1099511628211ULL;
    ShaderHash hash = seed;
    for (const std::byte byte : bytes) {
        hash ^= static_cast<ShaderHash>(std::to_integer<unsigned char>(byte));
        hash *= prime;
    }
    return hash;
}

ShaderHash hashString(std::string_view text, ShaderHash seed) {
    return hashBytes({reinterpret_cast<const std::byte*>(text.data()), text.size()}, seed);
}

ShaderPreprocessor::ShaderPreprocessor(ShaderPreprocessorConfig config,
                                       FileDependencyGraph& dependencies)
    : config_(std::move(config)), dependencies_(dependencies) {
    for (const VirtualPath& root : config_.includeSearchPaths) {
        if (!root.valid() || !FILE_SYSTEM.isMounted(root.scheme()) ||
            !FILE_SYSTEM.isDirectory(root)) {
            configValid_ = false;
            Log::error("ShaderPreprocessor",
                       "Include search path is not a mounted directory: %s",
                       root.string().c_str());
        }
    }
}

std::shared_ptr<PreprocessedShader>
ShaderPreprocessor::process(const ShaderPreprocessRequest& request) {
    if (!configValid_ || !request.source.sourcePath.valid() || request.source.source.empty()) {
        Log::error("ShaderPreprocessor", "Shader stage source is missing");
        return {};
    }
    std::vector<ShaderDefine> defines = request.defines;
    std::ranges::sort(defines, [](const ShaderDefine& left, const ShaderDefine& right) {
        return std::tie(left.name, left.value) < std::tie(right.name, right.value);
    });
    ShaderHash requestHash = hashString(request.source.sourcePath.string());
    requestHash = hashString(request.source.source, requestHash);
    hashValue(requestHash, request.source.stage);
    for (const ShaderDefine& define : defines) {
        requestHash = hashString(define.name, requestHash);
        requestHash = hashString(define.value, requestHash);
    }
    for (const VirtualPath& root : config_.includeSearchPaths)
        requestHash = hashString(root.string(), requestHash);
    if (const auto found = cache_.find(requestHash); found != cache_.end())
        return found->second;

    auto result = std::make_shared<PreprocessedShader>();
    result->sourcePath = request.source.sourcePath;
    result->stage = request.source.stage;
    result->entryPoint = request.source.entryPoint;
    std::unordered_set<std::string> visiting;
    if (!preprocessSource(request.source.sourcePath,
                          request.source.source,
                          config_.includeSearchPaths,
                          visiting,
                          *result)) {
        return {};
    }
    std::string defineSource;
    for (const ShaderDefine& define : defines)
        defineSource += "#define " + define.name + " " + define.value + "\n";
    if (result->source.starts_with("#version")) {
        const std::size_t lineEnd = result->source.find('\n');
        result->source.insert(lineEnd == std::string::npos ? result->source.size() : lineEnd + 1,
                              defineSource);
    } else {
        result->source.insert(0, defineSource);
    }
    result->sourceHash = hashString(result->source);
    result->sourceHash = hashString(result->entryPoint, result->sourceHash);
    hashValue(result->sourceHash, result->stage);
    result->cachePath =
        VirtualPath{"shader-preprocess://" + hashName(result->sourceHash) + ".glsl"};
    dependencies_.replaceDependencies(result->cachePath, result->dependencies);
    cache_[requestHash] = result;
    return result;
}

void ShaderPreprocessor::invalidate(std::span<const VirtualPath> paths) {
    std::erase_if(cache_, [&](const auto& entry) {
        if (!containsPath(paths, entry.second->cachePath))
            return false;
        dependencies_.remove(entry.second->cachePath);
        return true;
    });
}

void ShaderPreprocessor::clear() {
    for (const auto& [hash, shader] : cache_) {
        (void)hash;
        dependencies_.remove(shader->cachePath);
    }
    cache_.clear();
}

ShaderCompiler::ShaderCompiler(VirtualPath outputRoot, FileDependencyGraph& dependencies)
    : outputRoot_(std::move(outputRoot)), dependencies_(dependencies) {}

std::shared_ptr<SpirvBinary> ShaderCompiler::compile(const PreprocessedShader& shader,
                                                     const ShaderCompilerOptions& options) {
    ShaderHash key = shader.sourceHash;
    hashValue(key, options.optimization);
    key = hashString(options.compilerVersion, key);
    key = hashString(options.arguments, key);
    if (const auto found = cache_.find(key); found != cache_.end())
        return found->second;

    const std::string base =
        hashName(key) + (shader.stage == ShaderStage::Vertex ? ".vert" : ".frag");
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

std::optional<CompiledShaderHandle> CompiledShaderCache::find(CompiledShaderId id) const {
    if (const auto found = entries_.find(id); found != entries_.end()) {
        const Slot& slot = slots_[found->second];
        return CompiledShaderHandle{found->second, slot.generation};
    }
    return std::nullopt;
}

CompiledShaderHandle CompiledShaderCache::insert(CompiledShader shader) {
    if (const auto existing = find(shader.id))
        return *existing;
    auto slot = std::ranges::find_if(slots_, [](const Slot& value) { return !value.shader; });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index = static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    const CompiledShaderId id = shader.id;
    slot->shader = std::move(shader);
    entries_.emplace(id, index);
    return {index, slot->generation};
}

const CompiledShader& CompiledShaderCache::resolve(CompiledShaderHandle handle) const {
    if (handle.index >= slots_.size() || !slots_[handle.index].shader ||
        slots_[handle.index].generation != handle.generation) {
        Log::fatal("CompiledShaderCache", "Invalid or stale compiled shader handle");
    }
    return *slots_[handle.index].shader;
}

void CompiledShaderCache::removeId(CompiledShaderId id, std::vector<CompiledShaderId>& removed) {
    const auto found = entries_.find(id);
    if (found == entries_.end())
        return;
    Slot& slot = slots_[found->second];
    slot.shader.reset();
    ++slot.generation;
    entries_.erase(found);
    removed.push_back(id);
}

std::vector<CompiledShaderId>
CompiledShaderCache::invalidatePaths(std::span<const VirtualPath> paths) {
    std::vector<CompiledShaderId> removed;
    std::vector<CompiledShaderId> ids;
    for (const Slot& slot : slots_) {
        if (slot.shader && containsPath(paths, slot.shader->binaryPath))
            ids.push_back(slot.shader->id);
    }
    for (CompiledShaderId id : ids)
        removeId(id, removed);
    return removed;
}

void CompiledShaderCache::clear() {
    entries_.clear();
    for (Slot& slot : slots_) {
        if (slot.shader) {
            slot.shader.reset();
            ++slot.generation;
        }
    }
}

std::optional<ShaderProgramHandle> ShaderProgramCache::find(ShaderProgramId id) const {
    if (const auto found = entries_.find(id); found != entries_.end()) {
        const Slot& slot = slots_[found->second];
        return ShaderProgramHandle{found->second, slot.generation};
    }
    return std::nullopt;
}

ShaderProgramHandle ShaderProgramCache::insert(ShaderProgram program) {
    if (const auto existing = find(program.id))
        return *existing;
    auto slot = std::ranges::find_if(slots_, [](const Slot& value) { return !value.program; });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index = static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    const ShaderProgramId id = program.id;
    slot->program = std::move(program);
    entries_.emplace(id, index);
    return {index, slot->generation};
}

const ShaderProgram& ShaderProgramCache::resolve(ShaderProgramHandle handle) const {
    if (handle.index >= slots_.size() || !slots_[handle.index].program ||
        slots_[handle.index].generation != handle.generation) {
        Log::fatal("ShaderProgramCache", "Invalid or stale shader program handle");
    }
    return *slots_[handle.index].program;
}

void ShaderProgramCache::invalidate(std::span<const CompiledShaderId> shaders) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        Slot& slot = slots_[entry->second];
        const bool affected =
            slot.program && std::ranges::any_of(shaders, [&slot](auto id) {
                return slot.program->vertexId == id || slot.program->fragmentId == id;
            });
        if (affected) {
            slot.program.reset();
            ++slot.generation;
            entry = entries_.erase(entry);
        } else {
            ++entry;
        }
    }
}

void ShaderProgramCache::clear() {
    entries_.clear();
    for (Slot& slot : slots_) {
        if (slot.program) {
            slot.program.reset();
            ++slot.generation;
        }
    }
}

ShaderCompilePipeline::ShaderCompilePipeline(ShaderCompilePipelineConfig config,
                                             FileDependencyGraph& dependencies)
    : config_(std::move(config)), dependencies_(dependencies),
      preprocessor_(config_.preprocessorConfig, dependencies_),
      compiler_(config_.intermediateRoot, dependencies_) {}

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
    if (const auto cached = compiledShaders_.find(id))
        return *cached;
    const auto reflection = reflectSpirv(binaryPath);
    if (!reflection || reflection->stage != stage)
        return {};
    return compiledShaders_.insert(
        {id, stage, std::string{entryPoint}, *bytes, *reflection, binaryPath});
}

std::optional<ShaderCompilePipeline::CompiledStage>
ShaderCompilePipeline::compileStage(const Shader& shader,
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
            return std::nullopt;
        }
        const CompiledShaderHandle handle = loadCompiledShader(packaged, stage, "main", variant);
        return handle ? std::optional<CompiledStage>{{handle, packaged}} : std::nullopt;
    }

    const VirtualPath sourcePath =
        stage == ShaderStage::Vertex ? pass.program().vertexSource : pass.program().fragmentSource;
    const auto userSource = FILE_SYSTEM.readText(sourcePath);
    if (!userSource)
        return std::nullopt;

    ShaderHash generatedKey = hashString(shader.assetPath().string());
    generatedKey = hashString(pass.name(), generatedKey);
    generatedKey = hashString(sourcePath.string(), generatedKey);
    generatedKey = hashString(*userSource, generatedKey);
    hashValue(generatedKey, shader.revision());
    hashValue(generatedKey, stage);
    auto generatedEntry = generatedSources_.find(generatedKey);
    if (generatedEntry == generatedSources_.end()) {
        auto generated = shader_compiler::generateShaderStage(shader, pass, stage, *userSource);
        if (!generated)
            return std::nullopt;
        GeneratedSource entry;
        entry.cachePath = VirtualPath{"shader-generated://" + hashName(generatedKey) + ".glsl"};
        entry.source = std::make_shared<std::string>(std::move(*generated));
        const std::array dependencies{shader.assetPath(), sourcePath};
        dependencies_.replaceDependencies(entry.cachePath, dependencies);
        generatedEntry = generatedSources_.emplace(generatedKey, std::move(entry)).first;
    }

    ShaderPreprocessRequest request;
    request.source = {sourcePath, stage, "main", *generatedEntry->second.source};
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
        return std::nullopt;
    const auto spirv = compiler_.compile(*processed, config_.compilerOptions);
    if (!spirv)
        return std::nullopt;

    VirtualPath binaryPath = spirv->path;
    if (config_.mode == ShaderCompileMode::OfflineTool) {
        if (!FILE_SYSTEM.createDirectories(config_.packagedRoot) ||
            !FILE_SYSTEM.writeBinaryAtomic(packaged, spirv->bytecode)) {
            Log::error("ShaderCompilePipeline",
                       "Cannot write packaged SPIR-V: %s",
                       packaged.string().c_str());
            return std::nullopt;
        }
        const std::array dependency{spirv->path};
        dependencies_.replaceDependencies(packaged, dependency);
        binaryPath = packaged;
    }
    const CompiledShaderHandle handle =
        loadCompiledShader(binaryPath, stage, processed->entryPoint, variant);
    return handle ? std::optional<CompiledStage>{{handle, binaryPath}} : std::nullopt;
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
    layout.id = hashReflection(layout);
    return layout;
}

ShaderProgramHandle ShaderCompilePipeline::getOrCreate(const Shader& shader,
                                                       const ShaderPass& pass,
                                                       const ShaderVariantKey& variant) {
    const auto vertexStage = compileStage(shader, pass, ShaderStage::Vertex, variant);
    if (!vertexStage)
        return {};
    const auto fragmentStage = compileStage(shader, pass, ShaderStage::Fragment, variant);
    if (!fragmentStage)
        return {};
    const CompiledShader& vertex = compiledShaders_.resolve(vertexStage->handle);
    const CompiledShader& fragment = compiledShaders_.resolve(fragmentStage->handle);
    ShaderProgramId id = vertex.id;
    hashValue(id, fragment.id);
    hashValue(id, variant.keywordBits);
    hashValue(id, variant.meshFeatureBits);
    hashValue(id, variant.platformFeatureBits);
    if (const auto cached = programs_.find(id))
        return *cached;
    if (!validateSpirvReflection(
            shader, pass, vertexStage->binaryPath, fragmentStage->binaryPath)) {
        return {};
    }
    auto layout = mergeLayout(vertex, fragment);
    if (!layout)
        return {};
    return programs_.insert({id,
                             variant,
                             vertexStage->handle,
                             fragmentStage->handle,
                             vertex.id,
                             fragment.id,
                             std::move(*layout)});
}

const ShaderProgram& ShaderCompilePipeline::resolve(ShaderProgramHandle handle) const {
    return programs_.resolve(handle);
}

const CompiledShader& ShaderCompilePipeline::resolve(CompiledShaderHandle handle) const {
    return compiledShaders_.resolve(handle);
}

std::vector<CompiledShaderId> ShaderCompilePipeline::invalidate(const VirtualPath& changedFile) {
    std::vector<VirtualPath> affected = dependencies_.transitiveDependentsOf(changedFile);
    affected.push_back(changedFile);
    invalidateGeneratedSources(affected);
    preprocessor_.invalidate(affected);
    compiler_.invalidate(affected);
    std::vector<CompiledShaderId> removed = compiledShaders_.invalidatePaths(affected);
    programs_.invalidate(removed);
    return removed;
}

void ShaderCompilePipeline::invalidateGeneratedSources(std::span<const VirtualPath> paths) {
    std::erase_if(generatedSources_, [&](const auto& entry) {
        if (!containsPath(paths, entry.second.cachePath))
            return false;
        dependencies_.remove(entry.second.cachePath);
        return true;
    });
}

std::vector<CompiledShaderId> ShaderCompilePipeline::invalidateChanged() {
    std::vector<CompiledShaderId> result;
    for (const VirtualPath& path : dependencies_.consumeChangedFiles()) {
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
        dependencies_.remove(source.cachePath);
    }
    generatedSources_.clear();
    programs_.clear();
    compiledShaders_.clear();
    compiler_.clear();
    preprocessor_.clear();
}

} // namespace engine
