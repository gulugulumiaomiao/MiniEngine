#include "render/shader/ShaderCompilePipeline.h"

#include "core/filesystem/FileDependencyGraph.h"
#include "core/filesystem/FileSystem.h"
#include "core/hash.h"
#include "core/logging/Log.h"

#include <spirv_cross.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <tuple>
#include <utility>

namespace engine {

namespace {

struct ReflectionFailure final {};

template <typename... Args> [[noreturn]] void reflectionFail(const char* format, Args... args) {
    Log::error("SpirvReflection", format, args...);
    throw ReflectionFailure{};
}

ShaderValueType reflectValueType(const spirv_cross::SPIRType& type,
                                 const VirtualPath& path) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        switch (type.vecsize) {
        case 1: return ShaderValueType::Float;
        case 2: return ShaderValueType::Vec2;
        case 3: return ShaderValueType::Vec3;
        case 4: return ShaderValueType::Vec4;
        default: break;
        }
    }
    reflectionFail("Unsupported interface type in: %s", path.string().c_str());
}

std::string resourceName(const spirv_cross::Compiler& compiler,
                         const spirv_cross::Resource& resource) {
    return resource.name.empty() ? compiler.get_name(resource.id) : resource.name;
}

const ShaderStageVariable* findStageVariable(const std::vector<ShaderStageVariable>& variables,
                                             std::uint32_t location) {
    const auto found =
        std::ranges::find_if(variables, [location](const ShaderStageVariable& variable) {
            return variable.location == location;
        });
    return found == variables.end() ? nullptr : &*found;
}

void validateInterface(const std::vector<ShaderInterfaceVariable>& expected,
                       const std::vector<ShaderStageVariable>& reflected,
                       const VirtualPath& path,
                       std::string_view interfaceName) {
    if (expected.size() != reflected.size()) {
        reflectionFail("%s %.*s count does not match ShaderLab declaration",
                       path.string().c_str(),
                       static_cast<int>(interfaceName.size()),
                       interfaceName.data());
    }
    for (const ShaderInterfaceVariable& declared : expected) {
        const ShaderStageVariable* actual = findStageVariable(reflected, declared.location);
        if (!actual || actual->type != declared.type) {
            reflectionFail("%s %.*s location %u does not match ShaderLab declaration",
                           path.string().c_str(),
                           static_cast<int>(interfaceName.size()),
                           interfaceName.data(),
                           declared.location);
        }
    }
}

const ShaderDescriptorBinding*
findDescriptor(const SpirvReflection& reflection, std::uint32_t set, std::uint32_t binding) {
    const auto found = std::ranges::find_if(
        reflection.descriptors, [set, binding](const ShaderDescriptorBinding& descriptor) {
            return descriptor.set == set && descriptor.binding == binding;
        });
    return found == reflection.descriptors.end() ? nullptr : &*found;
}

bool validateMaterialBlock(std::span<const ShaderPropertyDesc> properties,
                           const SpirvReflection& reflection,
                           const VirtualPath& path) {
    const UniformBlockLayout layout = buildUniformBlockLayout(properties);
    if (layout.members.empty())
        return true;
    const ShaderDescriptorBinding* block = findDescriptor(reflection, 1, 0);
    if (!block)
        return false;
    if (block->type != ShaderDescriptorType::UniformBuffer)
        reflectionFail("%s set 1 binding 0 is not a uniform block", path.string().c_str());
    for (const UniformMemberLayout& expected : layout.members) {
        const auto member =
            std::ranges::find_if(block->members, [&expected](const ShaderUniformMember& actual) {
                return actual.name == expected.name;
            });
        if (member == block->members.end() || member->offset != expected.offset) {
            reflectionFail("%s uniform member %s has an unexpected offset",
                           path.string().c_str(),
                           expected.name.c_str());
        }
    }
    return true;
}

void validateTextureBindings(std::span<const ShaderPropertyDesc> properties,
                             const SpirvReflection& vertex,
                             const SpirvReflection& fragment) {
    std::uint32_t binding = 1;
    for (const ShaderPropertyDesc& property : properties) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const ShaderDescriptorBinding* descriptor = findDescriptor(fragment, 1, binding);
        if (!descriptor)
            descriptor = findDescriptor(vertex, 1, binding);
        if (!descriptor || descriptor->type != ShaderDescriptorType::CombinedImageSampler) {
            reflectionFail("Texture property %s is missing descriptor set 1 binding %u",
                           property.name.c_str(),
                           binding);
        }
        ++binding;
    }
}

} // namespace

std::optional<SpirvReflection>
ShaderCompilePipeline::reflectSpirv(std::span<const std::byte> bytecode,
                                    const VirtualPath& sourcePath) {
    try {
        if (bytecode.size() < 20 || bytecode.size() % sizeof(std::uint32_t) != 0)
            reflectionFail("Invalid SPIR-V byte count: %s", sourcePath.string().c_str());
        std::vector<std::uint32_t> words(bytecode.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytecode.data(), bytecode.size());
        if (words.front() != 0x07230203U)
            reflectionFail("Invalid SPIR-V magic: %s", sourcePath.string().c_str());

        spirv_cross::Compiler compiler(std::move(words));
        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        SpirvReflection result;
        switch (compiler.get_execution_model()) {
        case spv::ExecutionModelVertex: result.stage = ShaderStage::Vertex; break;
        case spv::ExecutionModelFragment: result.stage = ShaderStage::Fragment; break;
        default: reflectionFail("Unsupported shader stage: %s", sourcePath.string().c_str());
        }

        const auto reflectInterface = [&compiler, &sourcePath](const auto& stageResources) {
            std::vector<ShaderStageVariable> variables;
            variables.reserve(stageResources.size());
            for (const spirv_cross::Resource& resource : stageResources) {
                if (compiler.has_decoration(resource.id, spv::DecorationBuiltIn) ||
                    !compiler.has_decoration(resource.id, spv::DecorationLocation)) {
                    continue;
                }
                variables.push_back(
                    {resourceName(compiler, resource),
                     reflectValueType(compiler.get_type(resource.type_id), sourcePath),
                     compiler.get_decoration(resource.id, spv::DecorationLocation)});
            }
            std::ranges::sort(
                variables, [](const ShaderStageVariable& left, const ShaderStageVariable& right) {
                    return left.location < right.location;
                });
            return variables;
        };
        result.inputs = reflectInterface(resources.stage_inputs);
        result.outputs = reflectInterface(resources.stage_outputs);

        const auto reflectDescriptors = [&compiler, &result](const auto& shaderResources,
                                                             ShaderDescriptorType descriptorType,
                                                             bool reflectMembers = false) {
            for (const spirv_cross::Resource& resource : shaderResources) {
                if (!compiler.has_decoration(resource.id, spv::DecorationDescriptorSet) ||
                    !compiler.has_decoration(resource.id, spv::DecorationBinding)) {
                    continue;
                }
                ShaderDescriptorBinding descriptor;
                descriptor.name = resourceName(compiler, resource);
                descriptor.type = descriptorType;
                descriptor.set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
                descriptor.binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (reflectMembers) {
                    const spirv_cross::SPIRType& blockType =
                        compiler.get_type(resource.base_type_id);
                    descriptor.members.reserve(blockType.member_types.size());
                    for (std::uint32_t member = 0; member < blockType.member_types.size();
                         ++member) {
                        descriptor.members.push_back(
                            {compiler.get_member_name(resource.base_type_id, member),
                             compiler.type_struct_member_offset(blockType, member)});
                    }
                }
                result.descriptors.push_back(std::move(descriptor));
            }
        };

        reflectDescriptors(resources.uniform_buffers, ShaderDescriptorType::UniformBuffer, true);
        reflectDescriptors(resources.storage_buffers, ShaderDescriptorType::StorageBuffer, true);
        reflectDescriptors(resources.sampled_images, ShaderDescriptorType::CombinedImageSampler);
        reflectDescriptors(resources.separate_images, ShaderDescriptorType::SampledImage);
        reflectDescriptors(resources.storage_images, ShaderDescriptorType::SampledImage);
        reflectDescriptors(resources.subpass_inputs, ShaderDescriptorType::SampledImage);
        reflectDescriptors(resources.separate_samplers, ShaderDescriptorType::Sampler);
        std::ranges::sort(
            result.descriptors,
            [](const ShaderDescriptorBinding& left, const ShaderDescriptorBinding& right) {
                return std::tie(left.set, left.binding) < std::tie(right.set, right.binding);
            });
        return result;
    } catch (const spirv_cross::CompilerError& error) {
        Log::error("SpirvReflection",
                   "Cannot reflect %s: %s",
                   sourcePath.string().c_str(),
                   error.what());
    } catch (const ReflectionFailure&) {
    }
    return std::nullopt;
}

bool ShaderCompilePipeline::validateSpirvReflection(const Shader& shader,
                                                     const ShaderPass& pass,
                                                     const SpirvReflection& vertex,
                                                     const SpirvReflection& fragment,
                                                     const VirtualPath& vertexPath,
                                                     const VirtualPath& fragmentPath) {
    try {
        if (vertex.stage != ShaderStage::Vertex || fragment.stage != ShaderStage::Fragment)
            reflectionFail("SPIR-V stage does not match pass declaration");
        validateInterface(pass.vertexInput(), vertex.inputs, vertexPath, "vertex input");
        validateInterface(pass.varyings(), vertex.outputs, vertexPath, "stage output");
        validateInterface(pass.varyings(), fragment.inputs, fragmentPath, "stage input");
        validateInterface(
            pass.fragmentOutputs(), fragment.outputs, fragmentPath, "fragment output");
        const bool vertexHasMaterialBlock =
            validateMaterialBlock(shader.properties(), vertex, vertexPath);
        const bool fragmentHasMaterialBlock =
            validateMaterialBlock(shader.properties(), fragment, fragmentPath);
        if (!vertexHasMaterialBlock && !fragmentHasMaterialBlock &&
            !shader.uniformBlockLayout().members.empty()) {
            reflectionFail("Material uniform block is absent from both shader stages");
        }
        validateTextureBindings(shader.properties(), vertex, fragment);
        for (const ShaderDescriptorBinding& descriptor : vertex.descriptors) {
            if (const ShaderDescriptorBinding* other =
                    findDescriptor(fragment, descriptor.set, descriptor.binding);
                other && other->type != descriptor.type) {
                reflectionFail("Descriptor type differs between vertex and fragment stages");
            }
        }
    } catch (const ReflectionFailure&) {
        return false;
    }
    return true;
}

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

ShaderHash ShaderCompilePipeline::makeCompiledShaderPathKey(const VirtualPath& binaryPath,
                                                            ShaderStage stage,
                                                            std::string_view entryPoint,
                                                            const ShaderVariantKey& variant) {
    Hash64 hash = hashString("CompiledShaderPath");
    hash = hashString(binaryPath.string(), hash);
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

std::optional<CompiledShaderHandle>
ShaderCompilePipeline::CompiledShaderCache::findPath(ShaderHash pathKey) const {
    if (const auto found = pathEntries_.find(pathKey);
        found != pathEntries_.end() && pool_.find(found->second)) {
        return found->second;
    }
    return std::nullopt;
}

void ShaderCompilePipeline::CompiledShaderCache::rememberPath(ShaderHash pathKey,
                                                              CompiledShaderHandle handle) {
    if (pool_.find(handle))
        pathEntries_[pathKey] = handle;
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
    const CompiledShaderHandle handle = found->second;
    std::erase_if(pathEntries_,
                  [handle](const auto& entry) { return entry.second == handle; });
    (void)pool_.release(handle);
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
    pathEntries_.clear();
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
                                                               const ShaderVariantKey& variant,
                                                               std::span<const std::byte> bytecode) {
    if (bytecode.empty() || bytecode.size() % sizeof(std::uint32_t) != 0) {
        Log::error(
            "ShaderCompilePipeline", "Invalid or missing SPIR-V: %s", binaryPath.string().c_str());
        return {};
    }
    const CompiledShaderId id = makeCompiledShaderId(bytecode, stage, entryPoint, variant);
    if (const auto cached = compiledShadersCache_.find(id))
        return *cached;
    const auto reflection = reflectSpirv(bytecode, binaryPath);
    if (!reflection || reflection->stage != stage)
        return {};
    return compiledShadersCache_.insert({id,
                                         stage,
                                         std::string{entryPoint},
                                         std::vector<std::byte>{bytecode.begin(), bytecode.end()},
                                         *reflection,
                                         binaryPath});
}

CompiledShaderHandle ShaderCompilePipeline::loadPackagedShader(
    const VirtualPath& binaryPath,
    ShaderStage stage,
    std::string_view entryPoint,
    const ShaderVariantKey& variant) {
    const ShaderHash pathKey = makeCompiledShaderPathKey(binaryPath, stage, entryPoint, variant);
    if (const auto cached = compiledShadersCache_.findPath(pathKey))
        return *cached;
    const auto bytecode = FILE_SYSTEM.readBinary(binaryPath);
    if (!bytecode) {
        Log::error(
            "ShaderCompilePipeline", "Invalid or missing SPIR-V: %s", binaryPath.string().c_str());
        return {};
    }
    const CompiledShaderHandle handle =
        loadCompiledShader(binaryPath, stage, entryPoint, variant, *bytecode);
    if (handle)
        compiledShadersCache_.rememberPath(pathKey, handle);
    return handle;
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
        return loadPackagedShader(packaged, stage, "main", variant);
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
    return loadCompiledShader(
        binaryPath, stage, processed->entryPoint, variant, spirv->bytecode);
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
    if (!validateSpirvReflection(shader,
                                 pass,
                                 vertex.reflection,
                                 fragment.reflection,
                                 vertex.binaryPath,
                                 fragment.binaryPath)) {
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
