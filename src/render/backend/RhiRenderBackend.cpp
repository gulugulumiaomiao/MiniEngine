#include "render/backend/RhiRenderBackend.h"

#include "core/base/BuildConfig.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/backend/MaterialGpuCache.h"
#include "render/backend/MeshGpuCache.h"
#include "render/backend/PipelineCache.h"
#include "render/backend/RhiShaderCache.h"
#include "render/material/Material.h"
#include "render/render_graph/RenderGraph.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderCompiler.h"
#include "rhi/RhiFactory.h"
#include "runtime/window/Window.h"

#include <array>
#include <span>
#include <vector>

namespace engine {

RhiRenderBackend::RhiRenderBackend(Window& window, bool vsync) : window_(window) {
    const auto [width, height] = window.framebufferSize();
    rhi::Context context = rhi::createDefaultContext(window.nativeInstance(), window.nativeHandle(),
                                                     {width, height, vsync});
    device_ = std::move(context.device);
    swapchain_ = std::move(context.swapchain);
    createBindGroupLayouts();
    createFrameResources();
    if (!FILE_SYSTEM.mountDirectory("shader", MINI_GENERATED_SHADER_DIR, false)) {
        Log::fatal("RhiRenderBackend", "Cannot mount generated Shader directory: %s",
                   MINI_GENERATED_SHADER_DIR);
    }
    compiledShaderCache_ = std::make_unique<CompiledShaderCache>();
    shaderProgramCache_ = std::make_unique<ShaderProgramCache>(*compiledShaderCache_);
    rhiShaderCache_ = std::make_unique<RhiShaderCache>(*device_, *compiledShaderCache_);
    materialGpuCache_ =
        std::make_unique<MaterialGpuCache>(*device_, materialBindGroupLayout_, kFramesInFlight);
    meshGpuCache_ = std::make_unique<MeshGpuCache>(*device_);
    pipelineCache_ = std::make_unique<PipelineCache>(
        *device_, sceneBindGroupLayout_, materialBindGroupLayout_, *compiledShaderCache_,
        *shaderProgramCache_, *rhiShaderCache_);
}

RhiRenderBackend::~RhiRenderBackend() {
    if (!device_)
        return;
    device_->waitIdle();
    pipelineCache_->clear();
    pipelineCache_.reset();
    materialGpuCache_->clear();
    materialGpuCache_.reset();
    meshGpuCache_->clear();
    meshGpuCache_.reset();
    rhiShaderCache_->clear();
    rhiShaderCache_.reset();
    shaderProgramCache_->clear();
    shaderProgramCache_.reset();
    compiledShaderCache_->clear();
    compiledShaderCache_.reset();
    destroyFrameResources();
    device_->destroyBindGroupLayout(sceneBindGroupLayout_);
    device_->destroyBindGroupLayout(materialBindGroupLayout_);
    swapchain_.reset();
    device_.reset();
}

void RhiRenderBackend::createBindGroupLayouts() {
    constexpr rhi::ShaderVisibility allGraphics =
        rhi::ShaderVisibility::Vertex | rhi::ShaderVisibility::Fragment;
    const std::array sceneBindings{
        rhi::BindGroupLayoutEntry{0, rhi::BindingType::UniformBuffer, allGraphics},
        rhi::BindGroupLayoutEntry{1, rhi::BindingType::StorageBuffer,
                                  rhi::ShaderVisibility::Vertex},
    };
    sceneBindGroupLayout_ =
        device_->createBindGroupLayout({sceneBindings, "Scene bind group layout"});

    std::array<rhi::BindGroupLayoutEntry, 17> materialBindings{};
    materialBindings[0] = {0, rhi::BindingType::UniformBuffer, allGraphics};
    for (std::uint32_t binding = 1; binding < materialBindings.size(); ++binding) {
        materialBindings[binding] = {binding, rhi::BindingType::SampledTexture, allGraphics};
    }
    materialBindGroupLayout_ =
        device_->createBindGroupLayout({materialBindings, "Material bind group layout"});
}

void RhiRenderBackend::createFrameResources() {
    constexpr std::uint64_t objectBufferSize = sizeof(ObjectDrawData) * kMaxRenderObjects;
    for (FrameResources& frame : frames_) {
        frame.sceneBuffer = device_->createBuffer({
            .size = sizeof(SceneDrawData),
            .usage = rhi::BufferUsage::Uniform,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Scene uniforms",
        });
        frame.objectBuffer = device_->createBuffer({
            .size = objectBufferSize,
            .usage = rhi::BufferUsage::Storage,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Object draw data",
        });
        const std::array bindings{
            rhi::BindGroupEntry{.binding = 0,
                                .type = rhi::BindingType::UniformBuffer,
                                .buffer = frame.sceneBuffer,
                                .size = sizeof(SceneDrawData)},
            rhi::BindGroupEntry{.binding = 1,
                                .type = rhi::BindingType::StorageBuffer,
                                .buffer = frame.objectBuffer,
                                .size = objectBufferSize},
        };
        frame.sceneBindGroup =
            device_->createBindGroup({sceneBindGroupLayout_, bindings, "Scene bind group"});
    }
}

void RhiRenderBackend::destroyFrameResources() {
    for (FrameResources& frame : frames_) {
        if (frame.sceneBindGroup)
            device_->destroyBindGroup(frame.sceneBindGroup);
        if (frame.sceneBuffer)
            device_->destroyBuffer(frame.sceneBuffer);
        if (frame.objectBuffer)
            device_->destroyBuffer(frame.objectBuffer);
        frame = {};
    }
}

MeshDrawInfo RhiRenderBackend::prepareMesh(MeshHandle handle, Mesh& mesh) {
    return meshGpuCache_->prepare(handle, mesh);
}

void RhiRenderBackend::releaseMesh(MeshHandle handle) {
    meshGpuCache_->invalidate(handle);
}

rhi::GraphicsPipelineHandle RhiRenderBackend::pipelineForPass(const Shader& shader,
                                                              const ShaderPass& pass,
                                                              const ShaderVariantKey& variant,
                                                              const VertexLayout& vertexLayout) {
    refreshShaderCaches();
    return pipelineCache_->getOrCreate(shader, pass, variant, vertexLayout, swapchain_->format());
}

void RhiRenderBackend::refreshShaderCaches() {
    if (lastShaderPollSerial_ == frameSerial_)
        return;
    lastShaderPollSerial_ = frameSerial_;
    const std::vector<CompiledShaderId> changed = compiledShaderCache_->invalidateChanged();
    if (changed.empty())
        return;
    const std::uint64_t retireSerial = frameSerial_ + kFramesInFlight;
    pipelineCache_->invalidate(changed, retireSerial);
    rhiShaderCache_->invalidate(changed, retireSerial);
    shaderProgramCache_->invalidate(changed);
    Log::info("RhiRenderBackend", "Reloaded %zu changed shader stages", changed.size());
}

void RhiRenderBackend::uploadFrameData(FrameResources& frame, const DrawList& drawList) {
    if (drawList.objects.size() > kMaxRenderObjects) {
        Log::fatal("RhiRenderBackend", "DrawList exceeds kMaxRenderObjects");
    }
    device_->uploadBuffer(frame.sceneBuffer, std::as_bytes(std::span{&drawList.scene, 1}));
    if (!drawList.objects.empty()) {
        device_->uploadBuffer(frame.objectBuffer, std::as_bytes(std::span{drawList.objects}));
    }
}

void RhiRenderBackend::recordDrawCommands(FrameResources& frame, const DrawList& drawList) {
    const rhi::TextureHandle backBuffer = swapchain_->currentTexture();
    RenderGraph graph;
    graph.importTexture({
        .texture = backBuffer,
        .initialState = swapchain_->currentTextureState(),
        .finalState = rhi::ResourceState::Present,
        .aspect = rhi::TextureAspect::Color,
    });
    rhi::RenderingInfo rendering;
    rendering.renderArea = {0, 0, swapchain_->width(), swapchain_->height()};
    rendering.colorAttachments.push_back({swapchain_->currentTextureView(), rhi::LoadOp::Clear,
                                          rhi::StoreOp::Store, drawList.clearColor});
    graph.addGraphicsPass(
        "Forward", std::move(rendering),
        {{backBuffer, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [this, &frame, &drawList](rhi::IGraphicsCommandEncoder& encoder) {
            encoder.setViewport({0.0F, 0.0F, static_cast<float>(swapchain_->width()),
                                 static_cast<float>(swapchain_->height()), 0.0F, 1.0F});
            encoder.setScissor({0, 0, swapchain_->width(), swapchain_->height()});
            rhi::GraphicsPipelineHandle boundPipeline;
            MaterialHandle boundMaterial;
            for (const DrawItem& item : drawList.items) {
                if (item.pipeline != boundPipeline) {
                    encoder.bindPipeline(item.pipeline);
                    encoder.bindGroup(0, frame.sceneBindGroup);
                    boundPipeline = item.pipeline;
                }
                if (item.material != boundMaterial) {
                    const Material* material = MATERIAL_MANAGER.find(item.material);
                    if (!material) {
                        Log::fatal("RhiRenderBackend", "DrawItem contains a stale MaterialHandle");
                    }
                    encoder.bindGroup(1, materialGpuCache_->prepare(item.material, *material));
                    boundMaterial = item.material;
                }
                for (const DrawItem::VertexBuffer& vertex : item.vertexBuffers) {
                    encoder.bindVertexBuffer(vertex.binding, vertex.buffer);
                }
                encoder.bindIndexBuffer(item.indexBuffer, 0, item.indexFormat);
                encoder.drawIndexed(item.arguments);
            }
        });
    graph.execute(swapchain_->encoder());
}

void RhiRenderBackend::renderFrame(const DrawList& drawList) {
    if (swapchain_->beginFrame() == rhi::FrameStatus::OutOfDate) {
        recreateSwapchain();
        return;
    }
    FrameResources& frame = frames_[swapchain_->frameIndex()];
    pipelineCache_->collect(frameSerial_);
    rhiShaderCache_->collect(frameSerial_);
    materialGpuCache_->beginFrame(swapchain_->frameIndex());
    uploadFrameData(frame, drawList);
    recordDrawCommands(frame, drawList);
    const bool resized = window_.consumeResize();
    const rhi::FrameStatus status = swapchain_->endFrame();
    ++frameSerial_;
    if (status == rhi::FrameStatus::OutOfDate || resized)
        recreateSwapchain();
}

void RhiRenderBackend::recreateSwapchain() {
    window_.waitForUsableFramebuffer();
    if (window_.shouldClose())
        return;
    const auto [width, height] = window_.framebufferSize();
    device_->waitIdle();
    pipelineCache_->clear();
    swapchain_->resize(width, height);
}

void RhiRenderBackend::waitIdle() {
    device_->waitIdle();
}

} // namespace engine
