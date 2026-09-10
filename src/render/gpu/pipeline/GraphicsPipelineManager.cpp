#include "render/gpu/pipeline/GraphicsPipelineManager.h"

#include "core/hash.h"
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

rhi::CullMode toRhi(CullMode mode) {
    switch (mode) {
    case CullMode::Off: return rhi::CullMode::None;
    case CullMode::Front: return rhi::CullMode::Front;
    case CullMode::Back: return rhi::CullMode::Back;
    }
    return rhi::CullMode::None;
}

rhi::CompareOp toRhi(DepthCompare compare) {
    switch (compare) {
    case DepthCompare::Never: return rhi::CompareOp::Never;
    case DepthCompare::Less: return rhi::CompareOp::Less;
    case DepthCompare::LessEqual: return rhi::CompareOp::LessEqual;
    case DepthCompare::Equal: return rhi::CompareOp::Equal;
    case DepthCompare::Greater: return rhi::CompareOp::Greater;
    case DepthCompare::GreaterEqual: return rhi::CompareOp::GreaterEqual;
    case DepthCompare::Always: return rhi::CompareOp::Always;
    }
    return rhi::CompareOp::Always;
}

rhi::ColorWriteMask toRhiColorMask(std::string_view mask) {
    rhi::ColorWriteMask result = rhi::ColorWriteMask::None;
    if (mask.find('R') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Red;
    if (mask.find('G') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Green;
    if (mask.find('B') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Blue;
    if (mask.find('A') != std::string_view::npos)
        result = result | rhi::ColorWriteMask::Alpha;
    return result;
}

} // namespace

GraphicsPipelineManager::GraphicsPipelineManager() = default;
GraphicsPipelineManager::~GraphicsPipelineManager() = default;

bool GraphicsPipelineManager::initialize(rhi::IDevice& device,
                                         rhi::BindGroupLayoutHandle sceneLayout,
                                         rhi::BindGroupLayoutHandle materialLayout) {
    if (initialized()) {
        Log::error("GraphicsPipelineManager", "Manager is already initialized");
        return false;
    }
    sceneLayout_ = sceneLayout;
    materialLayout_ = materialLayout;
    factory_ = std::make_unique<GraphicsPipelineGpuFactory>(device);
    lastShaderPollSerial_ = ~std::uint64_t{};
    return true;
}

GraphicsPipelineCacheKey GraphicsPipelineManager::makeCacheKey(const ShaderProgram& program,
                                                               const ShaderPass& pass,
                                                               const VertexLayout& vertexLayout,
                                                               rhi::TextureFormat colorFormat) {
    const RenderStateDesc& state = pass.renderState();
    ShaderHash key = program.id;
    hashAppend(key, program.layout.id);
    hashAppend(key, colorFormat);
    hashAppend(key, state.cull);
    hashAppend(key, state.frontFace);
    hashAppend(key, state.fill);
    hashAppend(key, state.topology);
    hashAppend(key, state.depthWrite);
    hashAppend(key, state.depthTest);
    hashAppend(key, state.blend);
    key = hashString(state.colorMask, key);
    for (const VertexBinding& binding : vertexLayout.bindings) {
        hashAppend(key, binding.binding);
        hashAppend(key, binding.stride);
        hashAppend(key, binding.inputRate);
    }
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        hashAppend(key, attribute.location);
        hashAppend(key, attribute.offset);
        hashAppend(key, attribute.binding);
        hashAppend(key, attribute.format);
        hashAppend(key, attribute.semantic.type);
        hashAppend(key, attribute.semantic.index);
    }
    return key;
}

std::uint64_t GraphicsPipelineManager::makeFallbackKey(const Shader& shader,
                                                       const ShaderPass& pass,
                                                       const ShaderVariantKey& variant,
                                                       const VertexLayout& vertexLayout,
                                                       rhi::TextureFormat colorFormat) {
    ShaderHash key = hashString(shader.assetPath().string());
    key = hashString(pass.name(), key);
    hashAppend(key, pass.type());
    hashAppend(key, variant.keywordBits);
    hashAppend(key, variant.meshFeatureBits);
    hashAppend(key, variant.platformFeatureBits);
    hashAppend(key, colorFormat);
    for (const VertexBinding& binding : vertexLayout.bindings) {
        hashAppend(key, binding.binding);
        hashAppend(key, binding.stride);
        hashAppend(key, binding.inputRate);
    }
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        hashAppend(key, attribute.location);
        hashAppend(key, attribute.offset);
        hashAppend(key, attribute.binding);
        hashAppend(key, attribute.format);
        hashAppend(key, attribute.semantic.type);
        hashAppend(key, attribute.semantic.index);
    }
    return key;
}

rhi::GraphicsPipelineDesc
GraphicsPipelineManager::makeDescription(const ShaderPass& pass,
                                         const VertexLayout& vertexLayout,
                                         rhi::TextureFormat colorFormat,
                                         rhi::ShaderHandle vertexShader,
                                         std::string vertexEntry,
                                         rhi::ShaderHandle fragmentShader,
                                         std::string fragmentEntry) const {
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.vertexEntry = std::move(vertexEntry);
    desc.fragmentShader = fragmentShader;
    desc.fragmentEntry = std::move(fragmentEntry);
    desc.bindGroupLayouts = {sceneLayout_, materialLayout_};
    desc.colorFormats = {colorFormat};
    for (const VertexBinding& binding : vertexLayout.bindings) {
        desc.vertexBindings.push_back({binding.binding,
                                       binding.stride,
                                       binding.inputRate == VertexInputRate::Vertex
                                           ? rhi::VertexInputRate::Vertex
                                           : rhi::VertexInputRate::Instance});
    }
    for (const VertexAttribute& attribute : vertexLayout.attributes) {
        desc.vertexAttributes.push_back(
            {attribute.location, attribute.binding, toRhi(attribute.format), attribute.offset});
    }
    const RenderStateDesc& state = pass.renderState();
    desc.topology = state.topology == PrimitiveTopology::TriangleList
                        ? rhi::PrimitiveTopology::TriangleList
                        : rhi::PrimitiveTopology::LineList;
    desc.raster.cull = toRhi(state.cull);
    desc.raster.frontFace = state.frontFace == FrontFace::Clockwise
                                ? rhi::FrontFace::Clockwise
                                : rhi::FrontFace::CounterClockwise;
    desc.raster.fill =
        state.fill == FillMode::Solid ? rhi::FillMode::Solid : rhi::FillMode::Wireframe;
    desc.depthStencil.depthTestEnable = state.depthTest != DepthCompare::Always || state.depthWrite;
    desc.depthStencil.depthWriteEnable = state.depthWrite;
    desc.depthStencil.depthCompare = toRhi(state.depthTest);
    switch (state.blend) {
    case BlendMode::Off: desc.blend.mode = rhi::BlendMode::Off; break;
    case BlendMode::Alpha: desc.blend.mode = rhi::BlendMode::Alpha; break;
    case BlendMode::Additive: desc.blend.mode = rhi::BlendMode::Additive; break;
    case BlendMode::PremultipliedAlpha: desc.blend.mode = rhi::BlendMode::PremultipliedAlpha; break;
    }
    desc.blend.colorWriteMask = toRhiColorMask(state.colorMask);
    return desc;
}

rhi::GraphicsPipelineHandle GraphicsPipelineManager::resolve(const Shader& shader,
                                                             const ShaderPass& pass,
                                                             const ShaderVariantKey& variant,
                                                             const VertexLayout& vertexLayout,
                                                             rhi::TextureFormat colorFormat) {
    if (!initialized())
        return {};
    const std::uint64_t fallbackKey =
        makeFallbackKey(shader, pass, variant, vertexLayout, colorFormat);
    const ShaderProgramHandle programHandle =
        SHADER_GPU_MANAGER.getOrCreateProgram(shader, pass, variant);
    if (!programHandle) {
        Log::error("GraphicsPipelineManager",
                   "Shader program is unavailable for pass: %s",
                   pass.name().c_str());
        return {};
    }

    const ShaderProgram& program = SHADER_GPU_MANAGER.resolveProgram(programHandle);
    const GraphicsPipelineCacheKey key = makeCacheKey(program, pass, vertexLayout, colorFormat);
    if (const GraphicsPipelineGpuResource* cached = cache_.find(key)) {
        fallbackPipelines_.insert_or_assign(fallbackKey, key);
        return cached->pipeline;
    }

    const CompiledShader& vertex = SHADER_GPU_MANAGER.resolveCompiled(program.vertex);
    const CompiledShader& fragment = SHADER_GPU_MANAGER.resolveCompiled(program.fragment);
    GraphicsPipelineGpuResource created;
    if (!factory_->create(makeDescription(pass,
                                          vertexLayout,
                                          colorFormat,
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
    if (auto replaced = cache_.put(key, std::move(created)))
        factory_->release(*replaced);
    fallbackPipelines_.insert_or_assign(fallbackKey, key);
    const GraphicsPipelineGpuResource* stored = cache_.find(key);
    return stored ? stored->pipeline : rhi::GraphicsPipelineHandle{};
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
    for (auto& [key, resource] : pipelines) {
        std::erase_if(fallbackPipelines_, [key](const auto& item) { return item.second == key; });
        retired_.push_back({std::move(resource), retireSerial});
    }
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
    fallbackPipelines_.clear();
    for (auto& [unused, resource] : cache_.extractAll()) {
        (void)unused;
        factory_->release(resource);
    }
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
    lastShaderPollSerial_ = ~std::uint64_t{};
}

} // namespace engine
