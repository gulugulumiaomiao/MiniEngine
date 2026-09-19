#include "render/pipeline/MiniDeferredPipeline.h"

#include "core/logging/Log.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/material/MaterialGpuManager.h"
#include "render/pipeline/DeferredResolveShaders.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/queue/RenderQueue.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"
#include "render/renderer/DrawListBuilder.h"
#include "render/renderer/RenderFrameStats.h"
#include "render/scene/RenderScene.h"
#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace engine {

static_assert(FrameGpuManager::kFramesInFlight == 2,
              "MiniDeferredPipeline resolve bindings must match frames in flight");

MiniDeferredPipeline::~MiniDeferredPipeline() {
    releaseResources();
}

bool MiniDeferredPipeline::render(RenderContext& context) {
    DrawListBuilder builder;
    const SourceDrawData source = builder.extract(context.scene(), context);
    DrawList drawList = builder.prepare(source, context);
    sourceDrawGroups_ = drawList.sourceGroups;

    RenderFrameStats& frameStats = context.frameStats();
    frameStats.sourceDrawItems = source.items.size();
    frameStats.preparedDrawItems = drawList.items.size();

    resolveMaterialBindGroups(drawList, context.frameIndex());
    std::erase_if(drawList.items,
                  [](const DrawItem& item) { return !item.pipeline || !item.materialBindGroup; });
    drawList.groups.clear();
    for (const DrawItem& item : drawList.items) {
        drawList.groups[item.renderQueue].push_back(item);
    }
    staticBatcher_.process(drawList, context.device());
    const StaticBatcherStats& staticStats = staticBatcher_.stats();
    frameStats.staticSourceItems = staticStats.sourceItems;
    frameStats.staticCombinedDraws = staticStats.combinedDraws;
    frameStats.staticCacheHits = staticStats.cacheHits;
    frameStats.staticCacheMisses = staticStats.cacheMisses;

    FRAME_GPU_MANAGER.beginFrame(context.frameIndex());
    FRAME_GPU_MANAGER.upload(context.frameIndex(), drawList);

    std::vector<DrawItem> geometry = collectPassItems(
        drawList, RenderPhase::Forward, DrawFilter{RenderQueueRange::all()});
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    for (DrawItem& item : geometry) {
        (RenderQueueRange::opaque().contains(item.renderQueue) ? opaque : transparent)
            .push_back(std::move(item));
    }
    DrawSorter sorter;
    sorter.sort(opaque,
                SortingCriteria::RenderQueue | SortingCriteria::Pipeline |
                    SortingCriteria::Material | SortingCriteria::Mesh,
                context.scene());
    sorter.sort(transparent, SortingCriteria::BackToFront, context.scene());
    geometry.clear();
    geometry.insert(geometry.end(),
                    std::make_move_iterator(opaque.begin()),
                    std::make_move_iterator(opaque.end()));
    geometry.insert(geometry.end(),
                    std::make_move_iterator(transparent.begin()),
                    std::make_move_iterator(transparent.end()));

    if (!ensureResolveResources(context.device(), context.sceneColorFormat())) {
        return false;
    }

    RenderGraph graph;
    shadowOutput_ = {};
    ShadowCasterPass shadowPass{&shadowOutput_};
    shadowPass.execute(context, graph, drawList);
    const RgTextureHandle gBufferColor = graph.createTexture({
        .format = context.sceneColorFormat(),
        .width = context.sceneWidth(),
        .height = context.sceneHeight(),
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
        .aspect = rhi::TextureAspect::Color,
        .debugName = "DeferredGBufferColor",
    });
    RenderTarget& target = context.currentForwardTarget();
    const RgTextureHandle depth = target.importDepth(graph);
    const RgTextureHandle output = context.offscreenScene()
        ? target.importColor(graph, 0, rhi::ResourceState::ShaderRead)
        : graph.importTexture({
              .texture = context.swapchain().currentTexture(),
              .view = context.swapchain().currentTextureView(),
              .initialState = context.swapchain().currentTextureState(),
              .finalState = rhi::ResourceState::Present,
              .aspect = rhi::TextureAspect::Color,
          });
    if (!context.offscreenScene()) {
        context.markBackBufferWritten();
    }

    RgRenderingInfo geometryRendering;
    geometryRendering.renderArea = {0, 0, context.sceneWidth(), context.sceneHeight()};
    geometryRendering.colorAttachments.push_back(
        {gBufferColor, rhi::LoadOp::Clear, rhi::StoreOp::Store, drawList.clearColor});
    geometryRendering.depthAttachments.push_back(
        {depth, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0F});
    std::vector<RgResourceUsage> geometryResources{
        {gBufferColor, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
        {depth, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment},
    };
    if (shadowOutput_.castShadows && shadowOutput_.shadowMap.valid()) {
        geometryResources.push_back({shadowOutput_.shadowMap,
                                     rhi::TextureAspect::Depth,
                                     rhi::ResourceState::ShaderRead});
    }
    graph.addGraphicsPass(
        "DeferredGeometry",
        std::move(geometryRendering),
        std::move(geometryResources),
        [geometry = std::move(geometry), &context](rhi::IGraphicsCommandEncoder& encoder) mutable {
            encoder.setViewport({0.0F,
                                 0.0F,
                                 static_cast<float>(context.sceneWidth()),
                                 static_cast<float>(context.sceneHeight()),
                                 0.0F,
                                 1.0F});
            encoder.setScissor({0, 0, context.sceneWidth(), context.sceneHeight()});
            context.recordSubmission(drawFilteredItems(
                context.frameIndex(),
                geometry,
                FRAME_GPU_MANAGER.sceneBindGroup(context.frameIndex()),
                encoder,
                "DeferredGeometry"));
        });

    RgRenderingInfo resolveRendering;
    resolveRendering.renderArea = {0, 0, context.sceneWidth(), context.sceneHeight()};
    resolveRendering.colorAttachments.push_back(
        {output, rhi::LoadOp::Clear, rhi::StoreOp::Store, drawList.clearColor});
    graph.addGraphicsPass(
        "DeferredResolve",
        std::move(resolveRendering),
        {{gBufferColor, rhi::TextureAspect::Color, rhi::ResourceState::ShaderRead},
         {output, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [this, &context](rhi::IGraphicsCommandEncoder& encoder) {
            encoder.setViewport({0.0F,
                                 0.0F,
                                 static_cast<float>(context.sceneWidth()),
                                 static_cast<float>(context.sceneHeight()),
                                 0.0F,
                                 1.0F});
            encoder.setScissor({0, 0, context.sceneWidth(), context.sceneHeight()});
            rhi::DrawStateDesc state;
            state.raster.cull = rhi::CullMode::None;
            state.depthStencil.depthTestEnable = false;
            state.depthStencil.depthWriteEnable = false;
            encoder.bindPipeline(resolvePipeline_);
            encoder.bindGroup(0, resolveBindings_[context.frameIndex()].group);
            encoder.setDrawState(state);
            encoder.draw({.vertexCount = 3});
            context.recordSubmission({.renderItems = 1});
        });

    if (!graph.compile(context.rgTexturePool())) {
        Log::error("MiniDeferredPipeline", "RenderGraph setup failed: %s", graph.lastError().c_str());
        return false;
    }
    if (!updateResolveBinding(context.frameIndex(), graph.resolvedTextureView(gBufferColor))) {
        return false;
    }
    if (shadowOutput_.castShadows && shadowOutput_.shadowMap.valid()) {
        FRAME_GPU_MANAGER.bindShadowMap(context.frameIndex(),
                                        graph.resolvedTextureView(shadowOutput_.shadowMap));
    }
    frameStats.renderGraphPasses = graph.passCount();
    frameStats.renderGraphPlanCacheHit = graph.planCacheHit();
    frameStats.transientRenderTargets = context.rgTexturePool().inUseCount();
    graph.execute(context.encoder());
    graph.reset();
    return true;
}

void MiniDeferredPipeline::resolveMaterialBindGroups(DrawList& drawList,
                                                       std::uint32_t frameIndex) {
    MATERIAL_GPU_MANAGER.beginFrame(frameIndex);
    for (DrawItem& item : drawList.items) {
        item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.material);
        if (!item.materialBindGroup && item.fallbackPipeline && item.fallbackMaterial) {
            Log::error("MiniDeferredPipeline", "Using Error Material after GPU preparation failed");
            item.pipeline = item.fallbackPipeline;
            item.material = item.fallbackMaterial;
            item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.fallbackMaterial);
        }
    }
}

bool MiniDeferredPipeline::ensureResolveResources(rhi::IDevice& device,
                                                   rhi::TextureFormat outputFormat) {
    device_ = &device;
    if (!resolveLayout_) {
        const rhi::BindGroupLayoutEntry entry{.binding = 0,
                                               .type = rhi::BindingType::SampledTexture,
                                               .visibility = rhi::ShaderVisibility::Fragment};
        resolveLayout_ = device.createBindGroupLayout(
            {.entries = std::span{&entry, 1}, .debugName = "DeferredResolveLayout"});
        resolveSampler_ = device.createSampler({.addressU = rhi::SamplerAddressMode::ClampToEdge,
                                                .addressV = rhi::SamplerAddressMode::ClampToEdge});
        resolveVertexShader_ = device.createShader({
            .stage = rhi::ShaderStage::Vertex,
            .bytecode = std::as_bytes(std::span{deferred_shaders::kResolveVertexSpirv}),
            .debugName = "DeferredResolveVertex",
        });
        resolveFragmentShader_ = device.createShader({
            .stage = rhi::ShaderStage::Fragment,
            .bytecode = std::as_bytes(std::span{deferred_shaders::kResolveFragmentSpirv}),
            .debugName = "DeferredResolveFragment",
        });
    }
    if (!resolveLayout_ || !resolveSampler_ || !resolveVertexShader_ || !resolveFragmentShader_) {
        Log::error("MiniDeferredPipeline", "Cannot create deferred resolve resources");
        return false;
    }
    if (resolvePipeline_ && outputFormat_ == outputFormat) {
        return true;
    }
    if (resolvePipeline_) {
        device.destroyGraphicsPipeline(resolvePipeline_);
    }
    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = resolveVertexShader_;
    desc.fragmentShader = resolveFragmentShader_;
    desc.bindGroupLayouts = {resolveLayout_};
    desc.raster.cull = rhi::CullMode::None;
    desc.depthStencil.depthTestEnable = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.colorFormats = {outputFormat};
    resolvePipeline_ = device.createGraphicsPipeline(desc);
    outputFormat_ = outputFormat;
    if (!resolvePipeline_) {
        Log::error("MiniDeferredPipeline", "Cannot create deferred resolve pipeline");
        return false;
    }
    return true;
}

bool MiniDeferredPipeline::updateResolveBinding(std::uint32_t frameIndex,
                                                 rhi::TextureViewHandle view) {
    if (frameIndex >= resolveBindings_.size()) {
        return false;
    }
    ResolveBinding& binding = resolveBindings_[frameIndex];
    if (binding.group && binding.view == view) {
        return true;
    }
    if (binding.group) {
        device_->destroyBindGroup(binding.group);
    }
    const rhi::BindGroupEntry entry{.binding = 0,
                                     .type = rhi::BindingType::SampledTexture,
                                     .textureView = view,
                                     .sampler = resolveSampler_};
    binding.group = device_->createBindGroup(
        {.layout = resolveLayout_, .entries = std::span{&entry, 1}, .debugName = "DeferredGBuffer"});
    binding.view = binding.group ? view : rhi::TextureViewHandle{};
    return static_cast<bool>(binding.group);
}

void MiniDeferredPipeline::releaseFrameBindings() {
    if (!device_) {
        return;
    }
    for (ResolveBinding& binding : resolveBindings_) {
        if (binding.group) {
            device_->destroyBindGroup(binding.group);
        }
        binding = {};
    }
}

void MiniDeferredPipeline::releaseResources() {
    if (!device_) {
        return;
    }
    releaseFrameBindings();
    if (resolvePipeline_)
        device_->destroyGraphicsPipeline(resolvePipeline_);
    if (resolveVertexShader_)
        device_->destroyShader(resolveVertexShader_);
    if (resolveFragmentShader_)
        device_->destroyShader(resolveFragmentShader_);
    if (resolveSampler_)
        device_->destroySampler(resolveSampler_);
    if (resolveLayout_)
        device_->destroyBindGroupLayout(resolveLayout_);
    resolvePipeline_ = {};
    resolveVertexShader_ = {};
    resolveFragmentShader_ = {};
    resolveSampler_ = {};
    resolveLayout_ = {};
    outputFormat_ = rhi::TextureFormat::Undefined;
    device_ = nullptr;
}

void MiniDeferredPipeline::onSwapchainChanged() {
    staticBatcher_.clear();
    if (device_) {
        releaseFrameBindings();
        if (resolvePipeline_) {
            device_->destroyGraphicsPipeline(resolvePipeline_);
            resolvePipeline_ = {};
            outputFormat_ = rhi::TextureFormat::Undefined;
        }
    }
}

} // namespace engine
