#include "render/renderer/Renderer.h"

#include "core/base/BuildConfig.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/cache/MaterialGpuCache.h"
#include "render/cache/MeshGpuCache.h"
#include "render/cache/PipelineCache.h"
#include "render/cache/RhiShaderCache.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshBuilder.h"
#include "render/mesh/MeshManager.h"
#include "render/render_graph/RenderGraph.h"
#include "render/renderer/RenderScene.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "runtime/window/Window.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace engine {
namespace {

ShaderPassType passTypeForPhase(RenderPhase phase) {
    switch (phase) {
    case RenderPhase::Forward: return ShaderPassType::Forward;
    case RenderPhase::DepthOnly: return ShaderPassType::DepthOnly;
    case RenderPhase::ShadowCaster: return ShaderPassType::ShadowCaster;
    }
    return ShaderPassType::Forward;
}

int phaseOrder(RenderPhase phase) {
    switch (phase) {
    case RenderPhase::ShadowCaster: return 0;
    case RenderPhase::DepthOnly: return 1;
    case RenderPhase::Forward: return 2;
    }
    return 2;
}

} // namespace

Renderer::Renderer(Window& window, rhi::Context context)
    : window_(window), device_(std::move(context.device)),
      swapchain_(std::move(context.swapchain)) {
    if (!device_ || !swapchain_) {
        Log::fatal("Renderer", "RHI context is incomplete");
    }
    createBindGroupLayouts();
    createFrameResources();
    if (!FILE_SYSTEM.mountDirectory("shader", MINI_GENERATED_SHADER_DIR, false)) {
        Log::fatal(
            "Renderer", "Cannot mount generated Shader directory: %s", MINI_GENERATED_SHADER_DIR);
    }
    ShaderCompilePipelineConfig shaderConfig;
#if defined(MINI_RELEASE)
    shaderConfig.mode = ShaderCompileMode::PackagedRuntime;
#else
    shaderConfig.mode = ShaderCompileMode::DevelopmentRuntime;
#endif
    shaderConfig.preprocessorConfig.includeSearchPaths = {VirtualPath{"asset://shaders/include"}};
    shaderConfig.compilerOptions.compilerVersion = MINI_GLSLC_EXECUTABLE;
#if defined(MINI_DEBUG) || !defined(NDEBUG)
    shaderConfig.compilerOptions.optimization = ShaderOptimization::Debug;
#else
    shaderConfig.compilerOptions.optimization = ShaderOptimization::Release;
#endif
    shaderCompilePipeline_ = std::make_unique<ShaderCompilePipeline>(std::move(shaderConfig));
    rhiShaderCache_ = std::make_unique<RhiShaderCache>(*device_, *shaderCompilePipeline_);
    materialGpuCache_ =
        std::make_unique<MaterialGpuCache>(*device_, materialBindGroupLayout_, kFramesInFlight);
    meshGpuCache_ = std::make_unique<MeshGpuCache>(*device_);
    pipelineCache_ = std::make_unique<PipelineCache>(*device_,
                                                     sceneBindGroupLayout_,
                                                     materialBindGroupLayout_,
                                                     *shaderCompilePipeline_,
                                                     *rhiShaderCache_);
}

Renderer::~Renderer() {
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
    shaderCompilePipeline_->clear();
    shaderCompilePipeline_.reset();
    destroyFrameResources();
    device_->destroyBindGroupLayout(sceneBindGroupLayout_);
    device_->destroyBindGroupLayout(materialBindGroupLayout_);
    swapchain_.reset();
    device_.reset();
}

MeshHandle Renderer::createMesh(const MeshDesc& desc, const MeshData& data) {
    return MESH_MANAGER.insert(Mesh{desc, data});
}

MeshHandle Renderer::createProceduralMesh(const MeshBuildRecipe& recipe) {
    auto asset = MeshBuilder::buildAsset(recipe);
    if (!asset)
        return {};
    return MESH_MANAGER.insert(asset->instantiate());
}

MeshHandle Renderer::loadMesh(const VirtualPath& meshPath) {
    return MESH_MANAGER.load(meshPath);
}

MaterialHandle Renderer::loadMaterial(const VirtualPath& materialPath) {
    return MATERIAL_MANAGER.load(materialPath);
}

void Renderer::destroyMesh(MeshHandle handle) {
    releaseMesh(handle);
    (void)MESH_MANAGER.destroy(handle);
}

void Renderer::destroyMaterial(MaterialHandle handle) {
    MATERIAL_MANAGER.destroy(handle);
}

void Renderer::setMaterialFloat(MaterialHandle handle, std::string_view name, float value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setFloat(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialVec2(MaterialHandle handle,
                               std::string_view name,
                               const math::Vec2& value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setVec2(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialVec3(MaterialHandle handle,
                               std::string_view name,
                               const math::Vec3& value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setVec3(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialVec4(MaterialHandle handle,
                               std::string_view name,
                               const math::Vec4& value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setVec4(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialBool(MaterialHandle handle, std::string_view name, bool value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setBool(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialTexture(MaterialHandle handle, std::string_view name, std::string value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setTexture(name, std::move(value));
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialShader(MaterialHandle handle, const VirtualPath& shaderPath) {
    MATERIAL_MANAGER.setShader(handle, shaderPath);
}

void Renderer::renderFrame(const RenderScene& scene) {
    constexpr std::array phases{
        RenderPhase::ShadowCaster, RenderPhase::DepthOnly, RenderPhase::Forward};
    DrawList drawList;
    if (scene.camera()) {
        const RenderCamera& camera = *scene.camera();
        drawList.scene.viewProjection = camera.projection * camera.view;
        drawList.scene.cameraPosition = math::Vec4{camera.worldPosition, 1.0F};
        drawList.clearColor = camera.clearColor;
    }
    const auto directional = std::ranges::find_if(scene.lights(), [](const RenderLight& light) {
        return light.type == LightType::Directional;
    });
    if (directional != scene.lights().end()) {
        drawList.scene.directionalLightDirection = math::Vec4{directional->direction, 1.0F};
        drawList.scene.directionalLightColorIntensity =
            math::Vec4{directional->color, directional->intensity};
    }
    const auto point = std::ranges::find_if(
        scene.lights(), [](const RenderLight& light) { return light.type == LightType::Point; });
    if (point != scene.lights().end()) {
        drawList.scene.pointLightPositionRange = math::Vec4{point->position, point->range};
        drawList.scene.pointLightColorIntensity = math::Vec4{point->color, point->intensity};
    }
    drawList.objects.reserve(scene.objects().size());
    for (const RenderObject& object : scene.objects()) {
        if (scene.camera() && (object.layerMask & scene.camera()->cullingMask) == 0) {
            continue;
        }
        Mesh* meshInstance = MESH_MANAGER.find(object.mesh);
        if (!meshInstance) {
            Log::warn("Renderer", "Skipping object with an invalid MeshHandle");
            continue;
        }
        const MeshDrawInfo mesh = prepareMesh(object.mesh, *meshInstance);
        if (mesh.subMeshes.empty()) {
            Log::warn("Renderer", "Skipping Mesh without GPU draw data");
            continue;
        }
        const std::uint32_t objectIndex = static_cast<std::uint32_t>(drawList.objects.size());
        drawList.objects.push_back({object.transform});
        for (const MeshDrawInfo::Range& range : mesh.subMeshes) {
            const MaterialHandle materialHandle = object.material(range.materialSlot);
            const Material* material = MATERIAL_MANAGER.find(materialHandle);
            if (!material) {
                Log::warn("Renderer", "Skipping SubMesh with an invalid Material slot");
                continue;
            }
            const SubShader* subShader = material->shader().selectSubShader("MiniForward");
            if (!subShader) {
                Log::error("Renderer",
                           "Shader has no MiniForward SubShader: %s",
                           material->shader().name().c_str());
                continue;
            }
            for (const RenderPhase renderPhase : phases) {
                if (renderPhase == RenderPhase::ShadowCaster && !object.castShadow) {
                    continue;
                }
                const ShaderPass* shaderPass = subShader->findPass(passTypeForPhase(renderPhase));
                if (!shaderPass)
                    continue;
                const ShaderVariantKey variant = shaderPass->variantKey(material->keywords);
                const rhi::GraphicsPipelineHandle pipeline = pipelineForPass(
                    material->shader(), *shaderPass, variant, meshInstance->desc().vertexLayout);
                if (!pipeline) {
                    Log::error("Renderer",
                               "Skipping pass without a valid pipeline: %s",
                               shaderPass->name().c_str());
                    continue;
                }
                drawList.items.push_back({
                    .shaderPass = shaderPass,
                    .renderPhase = renderPhase,
                    .pipeline = pipeline,
                    .material = materialHandle,
                    .vertexBuffers = mesh.vertexBuffers,
                    .indexBuffer = mesh.indexBuffer,
                    .indexFormat = mesh.indexFormat,
                    .arguments = {.indexCount = range.indexCount,
                                  .instanceCount = 1,
                                  .firstIndex = range.firstIndex,
                                  .vertexOffset = range.vertexOffset,
                                  .firstInstance = objectIndex},
                    .renderQueue = material->renderQueue,
                });
            }
        }
    }
    std::ranges::stable_sort(drawList.items, [](const DrawItem& left, const DrawItem& right) {
        if (left.renderPhase != right.renderPhase) {
            return phaseOrder(left.renderPhase) < phaseOrder(right.renderPhase);
        }
        return left.renderQueue < right.renderQueue;
    });
    submitDrawList(drawList);
}

void Renderer::createBindGroupLayouts() {
    constexpr rhi::ShaderVisibility allGraphics =
        rhi::ShaderVisibility::Vertex | rhi::ShaderVisibility::Fragment;
    const std::array sceneBindings{
        rhi::BindGroupLayoutEntry{0, rhi::BindingType::UniformBuffer, allGraphics},
        rhi::BindGroupLayoutEntry{
            1, rhi::BindingType::StorageBuffer, rhi::ShaderVisibility::Vertex},
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

void Renderer::createFrameResources() {
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

void Renderer::destroyFrameResources() {
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

MeshDrawInfo Renderer::prepareMesh(MeshHandle handle, Mesh& mesh) {
    return meshGpuCache_->prepare(handle, mesh);
}

void Renderer::releaseMesh(MeshHandle handle) {
    meshGpuCache_->invalidate(handle);
}

rhi::GraphicsPipelineHandle Renderer::pipelineForPass(const Shader& shader,
                                                      const ShaderPass& pass,
                                                      const ShaderVariantKey& variant,
                                                      const VertexLayout& vertexLayout) {
    refreshShaderCaches();
    return pipelineCache_->getOrCreate(shader, pass, variant, vertexLayout, swapchain_->format());
}

void Renderer::refreshShaderCaches() {
    if (lastShaderPollSerial_ == frameSerial_)
        return;
    lastShaderPollSerial_ = frameSerial_;
    const std::vector<CompiledShaderId> changed = shaderCompilePipeline_->invalidateChanged();
    if (changed.empty())
        return;
    const std::uint64_t retireSerial = frameSerial_ + kFramesInFlight;
    pipelineCache_->invalidate(changed, retireSerial);
    rhiShaderCache_->invalidate(changed, retireSerial);
    Log::info("Renderer", "Reloaded %zu changed shader stages", changed.size());
}

void Renderer::uploadFrameData(FrameResources& frame, const DrawList& drawList) {
    if (drawList.objects.size() > kMaxRenderObjects) {
        Log::fatal("Renderer", "DrawList exceeds kMaxRenderObjects");
    }
    device_->uploadBuffer(frame.sceneBuffer, std::as_bytes(std::span{&drawList.scene, 1}));
    if (!drawList.objects.empty()) {
        device_->uploadBuffer(frame.objectBuffer, std::as_bytes(std::span{drawList.objects}));
    }
}

void Renderer::recordDrawCommands(FrameResources& frame, const DrawList& drawList) {
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
    rendering.colorAttachments.push_back({swapchain_->currentTextureView(),
                                          rhi::LoadOp::Clear,
                                          rhi::StoreOp::Store,
                                          drawList.clearColor});
    graph.addGraphicsPass(
        "Forward",
        std::move(rendering),
        {{backBuffer, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [this, &frame, &drawList](rhi::IGraphicsCommandEncoder& encoder) {
            encoder.setViewport({0.0F,
                                 0.0F,
                                 static_cast<float>(swapchain_->width()),
                                 static_cast<float>(swapchain_->height()),
                                 0.0F,
                                 1.0F});
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
                        Log::fatal("Renderer", "DrawItem contains a stale MaterialHandle");
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

void Renderer::submitDrawList(const DrawList& drawList) {
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

void Renderer::recreateSwapchain() {
    window_.waitForUsableFramebuffer();
    if (window_.shouldClose())
        return;
    const auto [width, height] = window_.framebufferSize();
    device_->waitIdle();
    pipelineCache_->clear();
    swapchain_->resize(width, height);
}

void Renderer::waitIdle() {
    device_->waitIdle();
}

} // namespace engine
