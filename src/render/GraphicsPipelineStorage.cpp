#include "render/GraphicsPipelineStorage.h"

#include "core/base/BuildConfig.h"
#include "core/logging/Log.h"
#include "core/math/hash.h"
#include "render/GraphicsPipelineStorageFactory.h"
#include "render/uniform/PipelineLayout.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace engine {
namespace {

rhi::VertexFormat toRhi(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32: return rhi::VertexFormat::Float32;
    case VertexFormat::Vec2Float32: return rhi::VertexFormat::Vec2Float32;
    case VertexFormat::Vec3Float32: return rhi::VertexFormat::Vec3Float32;
    case VertexFormat::Vec4Float32: return rhi::VertexFormat::Vec4Float32;
    case VertexFormat::UInt16x4: return rhi::VertexFormat::UInt16x4;
    case VertexFormat::UInt8x4Normalized: return rhi::VertexFormat::UInt8x4Normalized;
    }
    Log::fatal("GraphicsPipelineStorage", "Unsupported Mesh vertex format");
}

} // namespace

GraphicsPipelineStorage::GraphicsPipelineStorage() = default;
GraphicsPipelineStorage::~GraphicsPipelineStorage() = default;

bool GraphicsPipelineStorage::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("GraphicsPipelineStorage", "Manager is already initialized");
        return false;
    }
    ShaderCompilePipelineConfig config;
#if defined(MINI_PUBLISH)
    config.mode = ShaderCompileMode::PackagedRuntime;
#else
    config.mode = ShaderCompileMode::DevelopmentRuntime;
#endif
    config.preprocessorConfig.includeSearchPaths = {VirtualPath{"assets://shaders/include"}};
    config.compilerOptions.compilerVersion = MINI_GLSLC_EXECUTABLE;
#if !defined(NDEBUG)
    config.compilerOptions.optimization = ShaderOptimization::Debug;
#else
    config.compilerOptions.optimization = ShaderOptimization::Release;
#endif
    compilePipeline_ = std::make_unique<ShaderCompilePipeline>(std::move(config));
    device_ = &device;
    factory_ = std::make_unique<GraphicsPipelineStorageFactory>(device);
    lastShaderPollSerial_ = ~std::uint64_t{};
    return true;
}

Ref<ShaderProgram> GraphicsPipelineStorage::getOrCreateProgram(const Shader& shader,
                                                               const ShaderPass& pass,
                                                               const ShaderVariantKey& variant) {
    if (!initialized())
        return {};
    const RID programHandle = compilePipeline_->getOrCreate(shader, pass, variant);
    if (!programHandle) {
        Log::error("GraphicsPipelineStorage",
                   "Shader program is unavailable for pass: %s",
                   pass.name().c_str());
        return {};
    }
    const CompiledProgram& compiled = compilePipeline_->resolveProgram(programHandle);
    if (const auto found = programs_.find(compiled.id); found != programs_.end())
        return found->second;
    const CompiledShader& vertex = compilePipeline_->resolveCompiled(compiled.vertex);
    const CompiledShader& fragment = compilePipeline_->resolveCompiled(compiled.fragment);
    Ref<ShaderProgram> program = ShaderProgram::create(*device_, vertex, fragment, compiled.id);
    if (!program)
        return {};
    programs_.emplace(compiled.id, program);
    return program;
}

GraphicsPipelineStorageCacheKey
GraphicsPipelineStorage::makeCacheKey(const ShaderProgram& program,
                                      std::uint64_t vertexLayoutHash,
                                      rhi::PixelFormat colorFormat,
                                      rhi::PixelFormat depthFormat) {
    ShaderHash key = program.id();
    hashAppend(key, colorFormat);
    hashAppend(key, depthFormat);
    // Cull/frontFace/depthTest/depthWrite/blend/colorMask/topology/fill are dynamic states and do
    // not contribute to the pipeline key. The vertex layout hash comes from the Mesh's layout.
    hashAppend(key, vertexLayoutHash);
    return key;
}

rhi::GraphicsPipelineDesc
GraphicsPipelineStorage::makeDescription(const VertexLayout& vertexLayout,
                                         rhi::PixelFormat colorFormat,
                                         rhi::PixelFormat depthFormat,
                                         const ShaderProgram& program) const {
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = program.vertexShader();
    desc.vertexEntry = program.vertexEntry();
    desc.fragmentShader = program.fragmentShader();
    desc.fragmentEntry = program.fragmentEntry();
    desc.pipelineLayout = program.pipelineLayout()->rid();
    // Depth-only passes (ShadowCaster) pass Undefined as the color format and render without any
    // color attachment; toVulkan(Undefined) is not a valid attachment format.
    if (colorFormat != rhi::PixelFormat::Undefined)
        desc.colorFormats = {colorFormat};
    desc.depthFormat = depthFormat;
    for (const VertexStreamLayout& stream : vertexLayout.streams) {
        desc.vertexStreams.push_back({stream.binding,
                                      stream.location,
                                      toRhi(stream.format),
                                      vertexFormatSize(stream.format),
                                      stream.inputRate == VertexInputRate::Vertex
                                          ? rhi::VertexInputRate::Vertex
                                          : rhi::VertexInputRate::Instance});
    }
    return desc;
}

rhi::RID GraphicsPipelineStorage::resolve(const ShaderProgram& program,
                                          const Mesh& mesh,
                                          rhi::PixelFormat colorFormat,
                                          rhi::PixelFormat depthFormat) {
    if (!initialized())
        return {};
    const GraphicsPipelineStorageCacheKey key =
        makeCacheKey(program, mesh.vertexLayout().hash(), colorFormat, depthFormat);
    if (const GraphicsPipelineStorageEntry* cached = cache_.find(key))
        return cached->pipeline;

    GraphicsPipelineStorageEntry created;
    if (!factory_->create(
            makeDescription(mesh.vertexLayout(), colorFormat, depthFormat, program), created)) {
        return {};
    }
    created.program = program.id();
    auto stored = cache_.store(key, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    return stored.stored->pipeline;
}

void GraphicsPipelineStorage::refreshShaders(std::uint64_t frameSerial,
                                             std::uint64_t retireSerial) {
    if (!initialized() || lastShaderPollSerial_ == frameSerial)
        return;
    lastShaderPollSerial_ = frameSerial;
    const std::vector<CompiledShaderId> changed = compilePipeline_->invalidateChanged();
    if (changed.empty())
        return;
    // Coarse invalidation: any shader change retires every pipeline and program; they are rebuilt
    // on demand. Fine-grained matching is unnecessary because reloads are rare.
    for (auto& entry : cache_.extractAll())
        retired_.push_back({std::move(entry.second), retireSerial});
    for (auto& [id, program] : programs_)
        retiredPrograms_.push_back({std::move(program), retireSerial});
    programs_.clear();
    Log::info("GraphicsPipelineStorage", "Reloaded %zu changed shader stages", changed.size());
}

void GraphicsPipelineStorage::collect(std::uint64_t completedSerial) {
    if (!initialized())
        return;
    std::erase_if(retired_, [&](RetiredPipeline& retired) {
        if (retired.serial > completedSerial)
            return false;
        factory_->release(retired.resource);
        return true;
    });
    std::erase_if(retiredPrograms_, [completedSerial](const RetiredProgram& retired) {
        return retired.serial <= completedSerial;
    });
}

void GraphicsPipelineStorage::clear() {
    if (!initialized())
        return;
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
    for (RetiredPipeline& retired : retired_)
        factory_->release(retired.resource);
    retired_.clear();
}

void GraphicsPipelineStorage::shutdown() {
    if (!initialized())
        return;
    clear();
    retiredPrograms_.clear();
    programs_.clear();
    factory_.reset();
    compilePipeline_->clear();
    compilePipeline_.reset();
    device_ = nullptr;
    lastShaderPollSerial_ = ~std::uint64_t{};
}

} // namespace engine
