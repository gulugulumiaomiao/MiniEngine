#include "render/gpu/pipeline/GraphicsPipelineManager.h"

#include "core/math/hash.h"
#include "core/logging/Log.h"
#include "render/gpu/pipeline/GraphicsPipelineGpuFactory.h"
#include "render/gpu/shader/ShaderGpuManager.h"

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
    Log::fatal("GraphicsPipelineManager", "Unsupported Mesh vertex format");
}

} // namespace

GraphicsPipelineManager::GraphicsPipelineManager() = default;
GraphicsPipelineManager::~GraphicsPipelineManager() = default;

bool GraphicsPipelineManager::initialize(rhi::IDevice& device,
                                         rhi::BindGroupLayoutHandle sceneLayout,
                                         rhi::BindGroupLayoutHandle materialLayout,
                                         rhi::BindGroupLayoutHandle globalLayout) {
    if (initialized()) {
        Log::error("GraphicsPipelineManager", "Manager is already initialized");
        return false;
    }
    sceneLayout_ = sceneLayout;
    materialLayout_ = materialLayout;
    globalLayout_ = globalLayout;
    factory_ = std::make_unique<GraphicsPipelineGpuFactory>(device);
    lastShaderPollSerial_ = ~std::uint64_t{};
    return true;
}

GraphicsPipelineCacheKey GraphicsPipelineManager::makeCacheKey(const ShaderProgram& program,
                                                               std::uint64_t vertexLayoutHash,
                                                               rhi::PixelFormat colorFormat,
                                                               rhi::PixelFormat depthFormat) {
    ShaderHash key = program.id;
    hashAppend(key, program.layout.id);
    hashAppend(key, colorFormat);
    hashAppend(key, depthFormat);
    // Cull/frontFace/depthTest/depthWrite/blend/colorMask/topology/fill are dynamic states
    // and do not contribute to the pipeline key.
    // The Mesh caches its layout hash, so the vertex layout costs one mix instead of a
    // full walk over every binding and attribute on each resolve.
    hashAppend(key, vertexLayoutHash);
    return key;
}

rhi::GraphicsPipelineDesc
GraphicsPipelineManager::makeDescription(const VertexLayout& vertexLayout,
                                         rhi::PixelFormat colorFormat,
                                         rhi::PixelFormat depthFormat,
                                         rhi::ShaderHandle vertexShader,
                                         std::string vertexEntry,
                                         rhi::ShaderHandle fragmentShader,
                                         std::string fragmentEntry) const {
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.vertexEntry = std::move(vertexEntry);
    desc.fragmentShader = fragmentShader;
    desc.fragmentEntry = std::move(fragmentEntry);
    desc.bindGroupLayouts = {sceneLayout_, materialLayout_, globalLayout_};
    // Depth-only passes (ShadowCaster) pass Undefined as the color format and render without
    // any color attachment; toVulkan(Undefined) is not a valid attachment format.
    if (colorFormat != rhi::PixelFormat::Undefined) {
        desc.colorFormats = {colorFormat};
    }
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
    // cull/frontFace/depth/blend/colorMask/topology/fill are dynamic and are set by the
    // command buffer before each batch. Use benign defaults for pipeline creation.
    return desc;
}

rhi::GraphicsPipelineHandle GraphicsPipelineManager::resolve(const Shader& shader,
                                                             const ShaderPass& pass,
                                                             const ShaderVariantKey& variant,
                                                             const Mesh& mesh,
                                                             rhi::PixelFormat colorFormat,
                                                             rhi::PixelFormat depthFormat) {
    if (!initialized())
        return {};
    const ShaderProgramHandle programHandle =
        SHADER_GPU_MANAGER.getOrCreateProgram(shader, pass, variant);
    if (!programHandle) {
        Log::error("GraphicsPipelineManager",
                   "Shader program is unavailable for pass: %s",
                   pass.name().c_str());
        return {};
    }

    const ShaderProgram& program = SHADER_GPU_MANAGER.resolveProgram(programHandle);
    const GraphicsPipelineCacheKey key =
        makeCacheKey(program, mesh.vertexLayoutHash(), colorFormat, depthFormat);
    if (const GraphicsPipelineGpuResource* cached = cache_.find(key))
        return cached->pipeline;

    const CompiledShader& vertex = SHADER_GPU_MANAGER.resolveCompiled(program.vertex);
    const CompiledShader& fragment = SHADER_GPU_MANAGER.resolveCompiled(program.fragment);
    GraphicsPipelineGpuResource created;
    if (!factory_->create(makeDescription(mesh.desc().vertexLayout,
                                          colorFormat,
                                          depthFormat,
                                          SHADER_GPU_MANAGER.resolve(program.vertex),
                                          vertex.entryPoint,
                                          SHADER_GPU_MANAGER.resolve(program.fragment),
                                          fragment.entryPoint),
                          created)) {
        return {};
    }
    created.program = program.id;
    created.vertex = program.vertexId;
    created.fragment = program.fragmentId;
    auto stored = cache_.store(key, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    return stored.stored->pipeline;
}

void GraphicsPipelineManager::refreshShaders(std::uint64_t frameSerial,
                                             std::uint64_t retireSerial) {
    if (!initialized() || lastShaderPollSerial_ == frameSerial)
        return;
    lastShaderPollSerial_ = frameSerial;
    const std::vector<CompiledShaderId> changed =
        SHADER_GPU_MANAGER.invalidateChanged(retireSerial);
    if (changed.empty())
        return;
    invalidate(changed, retireSerial);
    Log::info("GraphicsPipelineManager", "Reloaded %zu changed shader stages", changed.size());
}

void GraphicsPipelineManager::invalidate(std::span<const CompiledShaderId> shaders,
                                         std::uint64_t retireSerial) {
    auto pipelines = cache_.extractIf(
        [&](GraphicsPipelineCacheKey, const GraphicsPipelineGpuResource& resource) {
            return std::ranges::find(shaders, resource.vertex) != shaders.end() ||
                   std::ranges::find(shaders, resource.fragment) != shaders.end();
        });
    for (auto& entry : pipelines)
        retired_.push_back({std::move(entry.second), retireSerial});
}

void GraphicsPipelineManager::collect(std::uint64_t completedSerial) {
    if (!initialized())
        return;
    std::erase_if(retired_, [&](RetiredPipeline& retired) {
        if (retired.serial > completedSerial)
            return false;
        factory_->release(retired.resource);
        return true;
    });
    SHADER_GPU_MANAGER.collect(completedSerial);
}

void GraphicsPipelineManager::clear() {
    if (!initialized())
        return;
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
    for (RetiredPipeline& retired : retired_)
        factory_->release(retired.resource);
    retired_.clear();
}

void GraphicsPipelineManager::shutdown() {
    if (!initialized())
        return;
    clear();
    factory_.reset();
    sceneLayout_ = {};
    materialLayout_ = {};
    globalLayout_ = {};
    lastShaderPollSerial_ = ~std::uint64_t{};
}

} // namespace engine
