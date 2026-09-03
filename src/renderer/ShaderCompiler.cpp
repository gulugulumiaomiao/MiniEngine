#include "renderer/ShaderCompiler.h"

#include "core/Log.h"
#include "core/io/FileSystem.h"
#include "renderer/ShaderGenerator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <tuple>
#include <utility>

namespace engine {
namespace {

template <typename Value>
void hashValue(ShaderHash& hash, const Value& value) {
    hash = hashBytes(
        {reinterpret_cast<const std::byte*>(&value), sizeof(Value)}, hash);
}

std::string normalizedPath(const VirtualPath& path) {
    return path.string();
}

std::optional<VirtualPath> resolveInclude(
    const VirtualPath& includingFile, std::string_view include,
    std::span<const VirtualPath> includePaths) {
    VirtualPath candidate = includingFile.parent().joined(include);
    if (FILE_SYSTEM.isFile(candidate)) {
        return candidate;
    }
    for (const VirtualPath& root : includePaths) {
        candidate = root.joined(include);
        if (FILE_SYSTEM.isFile(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool preprocessSource(const VirtualPath& path, std::string_view content,
                    std::span<const VirtualPath> includePaths,
                    std::unordered_set<std::string>& visiting,
                    PreprocessedShader& result) {
    const std::string key = normalizedPath(path);
    if (!visiting.insert(key).second) {
        Log::error("ShaderPreprocessor", "Cyclic include dependency: %s",
                   key.c_str());
        return false;
    }
    result.dependencies.emplace_back(key);

    std::istringstream lines{std::string{content}};
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 8, "#include") == 0) {
            const std::size_t quote = line.find('"', first + 8);
            const std::size_t endQuote =
                quote == std::string::npos ? std::string::npos
                                           : line.find('"', quote + 1);
            if (quote == std::string::npos || endQuote == std::string::npos) {
                Log::error("ShaderPreprocessor", "Malformed include in: %s",
                           key.c_str());
                return false;
            }
            const std::string include =
                line.substr(quote + 1, endQuote - quote - 1);
            const auto resolved = resolveInclude(path, include, includePaths);
            if (!resolved) {
                Log::error("ShaderPreprocessor", "Cannot resolve include %s from %s",
                           include.c_str(), key.c_str());
                return false;
            }
            const auto includedSource = FILE_SYSTEM.readText(*resolved);
            if (!includedSource) {
                Log::error("ShaderPreprocessor", "Cannot read include: %s",
                           resolved->string().c_str());
                return false;
            }
            if (!preprocessSource(*resolved, *includedSource, includePaths,
                                  visiting, result)) {
                return false;
            }
            continue;
        }
        result.source += line;
        result.source.push_back('\n');
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

ShaderAsset compileAsset(const Shader& shader) {
    ShaderAsset asset;
    asset.setAssetPath(shader.assetPath());
    asset.name = shader.name();
    asset.properties = shader.properties();
    return asset;
}

ShaderPassDesc compilePass(const ShaderPass& pass) {
    ShaderPassDesc desc;
    desc.name = pass.name();
    desc.type = pass.type();
    desc.program = pass.program();
    desc.vertexInput = pass.vertexInput();
    desc.varyings = pass.varyings();
    desc.fragmentOutputs = pass.fragmentOutputs();
    desc.features = pass.features();
    return desc;
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
    return hashBytes({reinterpret_cast<const std::byte*>(text.data()), text.size()},
                     seed);
}

std::shared_ptr<PreprocessedShader> ShaderPreprocessor::process(
    const ShaderCompileRequest& request) const {
    if (request.source.empty()) {
        Log::error("ShaderPreprocessor", "Shader source path must not be empty");
        return {};
    }
    auto result = std::make_shared<PreprocessedShader>();
    std::string defines;
    for (const ShaderDefine& define : request.defines) {
        defines += "#define " + define.name + " " + define.value + "\n";
    }
    const auto userSource = FILE_SYSTEM.readText(request.source);
    if (!userSource) {
        Log::error("ShaderPreprocessor", "Cannot read shader source: %s",
                   request.source.string().c_str());
        return {};
    }
    std::string source = *userSource;
    if ((request.shaderAsset == nullptr) != (request.shaderPass == nullptr)) {
        Log::error("ShaderPreprocessor",
                   "ShaderAsset and ShaderPass must be supplied together");
        return {};
    }
    if (request.shaderAsset && request.shaderPass) {
        const auto generated = shader_compiler::generateShaderStage(
            *request.shaderAsset, *request.shaderPass,
            buildUniformBlockLayout(request.shaderAsset->properties),
            request.stage, source);
        if (!generated) return {};
        source = *generated;
    }
    std::unordered_set<std::string> visiting;
    if (!preprocessSource(request.source, source, request.includePaths,
                          visiting, *result)) {
        return {};
    }
    if (result->source.starts_with("#version")) {
        const std::size_t lineEnd = result->source.find('\n');
        result->source.insert(lineEnd == std::string::npos ? result->source.size()
                                                          : lineEnd + 1,
                             defines);
    } else {
        result->source.insert(0, defines);
    }
    result->sourceHash = hashString(result->source);
    result->sourceHash = hashString(request.entryPoint, result->sourceHash);
    hashValue(result->sourceHash, request.stage);
    hashValue(result->sourceHash, request.target);
    result->sourceHash = hashString(request.compilerVersion, result->sourceHash);
    result->sourceHash = hashString(request.options, result->sourceHash);
    return result;
}

void ShaderDependencyGraph::track(
    CompiledShaderId shader,
    std::span<const VirtualPath> dependencies) {
    remove(shader);
    for (const VirtualPath& dependency : dependencies) {
        edges_[normalizedPath(dependency)].insert(shader);
    }
}

std::vector<CompiledShaderId> ShaderDependencyGraph::affectedBy(
    const VirtualPath& dependency) const {
    const auto found = edges_.find(normalizedPath(dependency));
    if (found == edges_.end()) {
        return {};
    }
    return {found->second.begin(), found->second.end()};
}

void ShaderDependencyGraph::remove(CompiledShaderId shader) {
    for (auto edge = edges_.begin(); edge != edges_.end();) {
        edge->second.erase(shader);
        if (edge->second.empty()) {
            edge = edges_.erase(edge);
        } else {
            ++edge;
        }
    }
}

void ShaderDependencyGraph::clear() {
    edges_.clear();
}

CompiledShaderId CompiledShaderCache::makeId(
    std::span<const std::byte> bytecode, ShaderStage stage,
    std::string_view entryPoint, const ShaderVariantKey& variant) {
    ShaderHash hash = hashBytes(bytecode);
    hashValue(hash, stage);
    hash = hashString(entryPoint, hash);
    hashValue(hash, variant.keywordBits);
    hashValue(hash, variant.meshFeatureBits);
    hashValue(hash, variant.platformFeatureBits);
    return hash;
}

CompiledShaderHandle CompiledShaderCache::getOrLoad(
    const VirtualPath& binaryPath, ShaderStage stage,
    std::string_view entryPoint, const ShaderVariantKey& variant) {
    const VirtualPath& path = binaryPath;
    const auto bytes = FILE_SYSTEM.readBinary(path);
    if (!bytes || bytes->empty() || bytes->size() % sizeof(std::uint32_t) != 0) {
        Log::error("CompiledShaderCache", "Invalid shader binary: %s",
                   path.string().c_str());
        return {};
    }
    const CompiledShaderId id = makeId(*bytes, stage, entryPoint, variant);
    if (const auto found = entries_.find(id); found != entries_.end()) {
        const Slot& slot = slots_[found->second];
        return {found->second, slot.generation};
    }

    auto slot = std::ranges::find_if(slots_,
                                     [](const Slot& value) {
                                         return !value.shader.has_value();
                                     });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index =
        static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    CompiledShader shader;
    shader.id = id;
    shader.stage = stage;
    shader.entryPoint = std::string{entryPoint};
    shader.bytecode = *bytes;
    const std::shared_ptr<SpirvReflection> reflection = reflectSpirv(path);
    if (!reflection) {
        return {};
    }
    shader.reflection = *reflection;
    if (shader.reflection.stage != stage) {
        Log::error("CompiledShaderCache", "Shader stage mismatch: %s",
                   path.string().c_str());
        return {};
    }
    shader.dependencies.push_back(path);
    std::error_code error;
    if (const auto physical = FILE_SYSTEM.resolvePhysicalPath(path)) {
        slot->timestamp = std::filesystem::last_write_time(*physical, error);
    }
    slot->shader = std::move(shader);
    entries_[id] = index;
    dependencies_.track(id, slot->shader->dependencies);
    return {index, slot->generation};
}

const CompiledShader& CompiledShaderCache::resolve(
    CompiledShaderHandle handle) const {
    if (handle.index >= slots_.size()) {
        Log::fatal("CompiledShaderCache", "Invalid compiled shader handle");
    }
    const Slot& slot = slots_[handle.index];
    if (!slot.shader || slot.generation != handle.generation) {
        Log::fatal("CompiledShaderCache", "Stale compiled shader handle");
    }
    return *slot.shader;
}

void CompiledShaderCache::removeId(CompiledShaderId id,
                                   std::vector<CompiledShaderId>& removed) {
    const auto found = entries_.find(id);
    if (found == entries_.end()) {
        return;
    }
    Slot& slot = slots_[found->second];
    slot.shader.reset();
    ++slot.generation;
    entries_.erase(found);
    dependencies_.remove(id);
    removed.push_back(id);
}

std::vector<CompiledShaderId> CompiledShaderCache::invalidateDependency(
    const VirtualPath& dependency) {
    std::vector<CompiledShaderId> removed;
    const std::vector<CompiledShaderId> affected =
        dependencies_.affectedBy(dependency);
    for (const CompiledShaderId id : affected) {
        removeId(id, removed);
    }
    return removed;
}

std::vector<CompiledShaderId> CompiledShaderCache::invalidateChanged() {
    std::vector<VirtualPath> changed;
    for (const Slot& slot : slots_) {
        if (!slot.shader || slot.shader->dependencies.empty()) {
            continue;
        }
        std::error_code error;
        const auto physical = FILE_SYSTEM.resolvePhysicalPath(
            slot.shader->dependencies.front());
        if (!physical) continue;
        const auto timestamp = std::filesystem::last_write_time(*physical, error);
        if (!error && timestamp != slot.timestamp) {
            changed.push_back(slot.shader->dependencies.front());
        }
    }
    std::vector<CompiledShaderId> removed;
    for (const VirtualPath& path : changed) {
        std::vector<CompiledShaderId> invalidated = invalidateDependency(path);
        removed.insert(removed.end(), invalidated.begin(), invalidated.end());
    }
    std::ranges::sort(removed);
    removed.erase(std::unique(removed.begin(), removed.end()), removed.end());
    return removed;
}

void CompiledShaderCache::clear() {
    entries_.clear();
    dependencies_.clear();
    for (Slot& slot : slots_) {
        if (slot.shader) {
            slot.shader.reset();
            ++slot.generation;
        }
    }
}

ShaderProgramCache::ShaderProgramCache(CompiledShaderCache& shaders)
    : shaders_(shaders) {}

CompiledShaderHandle ShaderProgramCache::compileStage(
    const Shader& shader, const ShaderPass& pass, ShaderStage stage,
    const ShaderVariantKey& variant, VirtualPath& binaryPath) {
    const ShaderAsset asset = compileAsset(shader);
    const ShaderPassDesc passDesc = compilePass(pass);
    ShaderCompileRequest request;
    request.source = stage == ShaderStage::Vertex
                         ? pass.program().vertexSource
                         : pass.program().fragmentSource;
    request.stage = stage;
    request.shaderAsset = &asset;
    request.shaderPass = &passDesc;
#if defined(MINI_GLSLC_EXECUTABLE)
    request.compilerVersion = MINI_GLSLC_EXECUTABLE;
#endif
#if defined(MINI_DEBUG) || !defined(NDEBUG)
    request.options = "--target-env=vulkan1.3 -O0 -g";
#else
    request.options = "--target-env=vulkan1.3 -O -g";
#endif
    const std::vector<std::string>& keywords = pass.keywordSchema().keywords();
    for (std::size_t bit = 0; bit < keywords.size(); ++bit) {
        if ((variant.keywordBits & (std::uint64_t{1} << bit)) != 0) {
            request.defines.push_back({keywords[bit], "1"});
        }
    }
    request.defines.push_back(
        {"MINI_MESH_FEATURE_BITS", std::to_string(variant.meshFeatureBits)});
    request.defines.push_back(
        {"MINI_PLATFORM_FEATURE_BITS",
         std::to_string(variant.platformFeatureBits)});

    ShaderPreprocessor preprocessor;
    const std::shared_ptr<PreprocessedShader> processed =
        preprocessor.process(request);
    if (!processed) return {};

    std::ostringstream name;
    name << "runtime/" << std::hex << std::setfill('0') << std::setw(16)
         << processed->sourceHash
         << (stage == ShaderStage::Vertex ? ".vert" : ".frag");
    const VirtualPath sourcePath{"shader://" + name.str() + ".glsl"};
    binaryPath = VirtualPath{"shader://" + name.str() + ".spv"};
    if (!FILE_SYSTEM.isFile(binaryPath)) {
        if (!FILE_SYSTEM.writeText(sourcePath, processed->source)) {
            Log::error("ShaderProgramCache", "Cannot write generated Shader: %s",
                       sourcePath.string().c_str());
            return {};
        }
        const auto sourcePhysical = FILE_SYSTEM.resolvePhysicalPath(sourcePath);
        const auto binaryPhysical = FILE_SYSTEM.resolvePhysicalPath(binaryPath);
        if (!sourcePhysical || !binaryPhysical) {
            Log::error("ShaderProgramCache",
                       "Cannot resolve generated Shader paths");
            return {};
        }
#if !defined(MINI_GLSLC_EXECUTABLE)
        Log::error("ShaderProgramCache", "glslc executable is not configured");
        return {};
#else
        // cmd.exe requires an extra pair of outer quotes when the executable
        // and its arguments contain quoted paths.
        std::string command = "\"" + quotedPath(MINI_GLSLC_EXECUTABLE);
        command += " --target-env=vulkan1.3 ";
#if defined(MINI_DEBUG) || !defined(NDEBUG)
        command += "-O0 -g ";
#else
        // Reflection validation needs member names, so optimized builds keep
        // debug names while still using release optimization.
        command += "-O -g ";
#endif
        command += stage == ShaderStage::Vertex
                       ? "-fshader-stage=vert "
                       : "-fshader-stage=frag ";
        command += quotedPath(*sourcePhysical) + " -o " +
                   quotedPath(*binaryPhysical);
        command += '"';
        Log::info("ShaderProgramCache", "Compiling Shader on demand: %s/%s",
                  shader.name().c_str(), pass.name().c_str());
        if (std::system(command.c_str()) != 0 ||
            !FILE_SYSTEM.isFile(binaryPath)) {
            Log::error("ShaderProgramCache", "Shader compilation failed: %s",
                       request.source.string().c_str());
            return {};
        }
#endif
    }
    return shaders_.getOrLoad(binaryPath, stage, "main", variant);
}

std::optional<ShaderProgramLayout> ShaderProgramCache::mergeLayout(
    const CompiledShader& vertex, const CompiledShader& fragment) {
    ShaderProgramLayout layout;
    layout.vertexInputs = vertex.reflection.inputs;
    layout.fragmentOutputs = fragment.reflection.outputs;
    layout.descriptors = vertex.reflection.descriptors;
    for (const ShaderDescriptorBinding& descriptor :
         fragment.reflection.descriptors) {
        const auto existing = std::ranges::find_if(
            layout.descriptors,
            [&descriptor](const ShaderDescriptorBinding& value) {
                return value.set == descriptor.set &&
                       value.binding == descriptor.binding;
            });
        if (existing == layout.descriptors.end()) {
            layout.descriptors.push_back(descriptor);
        } else if (existing->type != descriptor.type) {
            Log::error("ShaderProgramCache",
                       "Descriptor type differs between shader stages");
            return std::nullopt;
        } else if (existing->members.empty()) {
            existing->members = descriptor.members;
        }
    }
    std::ranges::sort(layout.descriptors,
                      [](const ShaderDescriptorBinding& left,
                         const ShaderDescriptorBinding& right) {
                          return std::tie(left.set, left.binding) <
                                 std::tie(right.set, right.binding);
                      });
    layout.id = hashReflection(layout);
    return layout;
}

ShaderProgramHandle ShaderProgramCache::getOrCreate(
    const Shader& shader, const ShaderPass& pass,
    const ShaderVariantKey& variant) {
    VirtualPath vertexPath;
    VirtualPath fragmentPath;
    const CompiledShaderHandle vertex =
        compileStage(shader, pass, ShaderStage::Vertex, variant, vertexPath);
    const CompiledShaderHandle fragment =
        compileStage(shader, pass, ShaderStage::Fragment, variant, fragmentPath);
    if (!vertex || !fragment) {
        Log::error("ShaderProgramCache", "Shader program failed to load: %s",
                   pass.name().c_str());
        return {};
    }
    const CompiledShader& vertexShader = shaders_.resolve(vertex);
    const CompiledShader& fragmentShader = shaders_.resolve(fragment);
    ShaderHash id = vertexShader.id;
    hashValue(id, fragmentShader.id);
    hashValue(id, variant.keywordBits);
    hashValue(id, variant.meshFeatureBits);
    hashValue(id, variant.platformFeatureBits);
    if (const auto found = entries_.find(id); found != entries_.end()) {
        return {found->second, slots_[found->second].generation};
    }
    const ShaderAsset asset = compileAsset(shader);
    const ShaderPassDesc passDesc = compilePass(pass);
    if (!validateSpirvReflection(asset, passDesc, vertexPath, fragmentPath)) {
        Log::error("ShaderProgramCache", "Shader reflection validation failed: %s",
                   pass.name().c_str());
        return {};
    }
    std::optional<ShaderProgramLayout> layout =
        mergeLayout(vertexShader, fragmentShader);
    if (!layout) {
        return {};
    }
    auto slot = std::ranges::find_if(slots_,
                                     [](const Slot& value) {
                                         return !value.program.has_value();
                                     });
    if (slot == slots_.end()) {
        slots_.emplace_back();
        slot = std::prev(slots_.end());
    }
    const std::uint32_t index =
        static_cast<std::uint32_t>(std::distance(slots_.begin(), slot));
    slot->program =
        ShaderProgram{id, variant, vertex, fragment, vertexShader.id,
                      fragmentShader.id,
                      std::move(*layout)};
    entries_[id] = index;
    return {index, slot->generation};
}

const ShaderProgram& ShaderProgramCache::resolve(
    ShaderProgramHandle handle) const {
    if (handle.index >= slots_.size()) {
        Log::fatal("ShaderProgramCache", "Invalid shader program handle");
    }
    const Slot& slot = slots_[handle.index];
    if (!slot.program || slot.generation != handle.generation) {
        Log::fatal("ShaderProgramCache", "Stale shader program handle");
    }
    return *slot.program;
}

void ShaderProgramCache::invalidate(
    std::span<const CompiledShaderId> shaders) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        Slot& slot = slots_[entry->second];
        const bool affected = slot.program && std::ranges::any_of(
            shaders, [&slot](CompiledShaderId id) {
                return slot.program->vertexId == id ||
                       slot.program->fragmentId == id;
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

ShaderCookedAsset buildCookedShaderAsset(
    const ShaderAsset& asset,
    std::span<const std::pair<const ShaderPass*, ShaderProgram>> programs) {
    ShaderCookedAsset cooked;
    cooked.name = asset.name;
    cooked.properties = asset.properties;
    cooked.materialLayout = buildUniformBlockLayout(asset.properties);
    for (const SubShaderDesc& subShader : asset.subShaders) {
        for (const ShaderPassAsset& passAsset : subShader.passes) {
            const ShaderPassDesc& pass = passAsset.pass;
            ShaderCookedPass cookedPass{pass.name, pass.type,
                                        passAsset.renderState, {}};
            for (const auto& [runtimePass, program] : programs) {
                if (runtimePass && runtimePass->name() == pass.name) {
                    cookedPass.variants.push_back(
                        {program.variant, program.vertexId,
                         program.fragmentId, program.layout.id});
                }
            }
            cooked.passes.push_back(std::move(cookedPass));
        }
    }
    return cooked;
}

} // namespace engine
