#include "render/pipeline/MiniForwardPipeline.h"

#include "core/logging/Log.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/material/MaterialGpuManager.h"
#include "render/gpu/mesh/MeshGpuManager.h"
#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "render/scene/RenderScene.h"
#include "rhi/api/Swapchain.h"

#include <algorithm>
#include <array>
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

void MiniForwardPipeline::render(RenderContext& context) {
    constexpr std::array phases{
        RenderPhase::ShadowCaster, RenderPhase::DepthOnly, RenderPhase::Forward};
    DrawList drawList;
    const RenderScene& scene = context.scene();
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
        const MeshDrawInfo mesh = MESH_GPU_MANAGER.resolve(object.mesh);
        if (mesh.subMeshes.empty()) {
            Log::warn("Renderer", "Skipping Mesh without GPU draw data");
            continue;
        }
        const std::uint32_t objectIndex = static_cast<std::uint32_t>(drawList.objects.size());
        drawList.objects.push_back({object.transform});
        struct ResolvedMaterialPass {
            MaterialHandle material;
            const ShaderPass* pass{};
            rhi::GraphicsPipelineHandle pipeline;

            [[nodiscard]] explicit operator bool() const { return static_cast<bool>(pipeline); }
        };
        const auto resolveMaterialPass = [&](MaterialHandle materialHandle,
                                             RenderPhase renderPhase) -> ResolvedMaterialPass {
            const Material* material = MATERIAL_MANAGER.find(materialHandle);
            if (!material)
                return {};
            const SubShader* subShader = material->shader().selectSubShader("MiniForward");
            if (!subShader)
                return {};
            const ShaderPass* shaderPass = subShader->findPass(passTypeForPhase(renderPhase));
            if (!shaderPass)
                return {};
            const ShaderVariantKey variant = shaderPass->variantKey(material->keywords);
            return {
                materialHandle,
                shaderPass,
                GRAPHICS_PIPELINE_MANAGER.resolve(material->shader(),
                                                  *shaderPass,
                                                  variant,
                                                  meshInstance->desc().vertexLayout,
                                                  context.swapchain().format(),
                                                  context.currentForwardTarget().depthFormat()),
            };
        };
        for (const MeshDrawInfo::Range& range : mesh.subMeshes) {
            const MaterialHandle requestedMaterial = object.material(range.materialSlot);
            for (const RenderPhase renderPhase : phases) {
                if (renderPhase == RenderPhase::ShadowCaster && !object.castShadow) {
                    continue;
                }
                ResolvedMaterialPass resolved = resolveMaterialPass(requestedMaterial, renderPhase);
                ResolvedMaterialPass fallback;
                if (renderPhase == RenderPhase::Forward) {
                    fallback = resolveMaterialPass(MATERIAL_MANAGER.errorMaterial(), renderPhase);
                    if (!resolved) {
                        Log::error("Renderer", "Using Error Material for an unavailable material");
                        resolved = fallback;
                    }
                }
                if (!resolved) {
                    continue;
                }
                const Material* material = MATERIAL_MANAGER.find(resolved.material);
                drawList.items.push_back({
                    .shaderPass = resolved.pass,
                    .renderPhase = renderPhase,
                    .pipeline = resolved.pipeline,
                    .material = resolved.material,
                    .fallbackPipeline = fallback.pipeline,
                    .fallbackMaterial = fallback.material,
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
    submitDrawList(context, drawList);
}

void MiniForwardPipeline::submitDrawList(RenderContext& context, DrawList drawList) {
    MATERIAL_GPU_MANAGER.beginFrame(context.frameIndex());
    for (DrawItem& item : drawList.items) {
        item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.material);
        if (!item.materialBindGroup && item.fallbackPipeline && item.fallbackMaterial) {
            Log::error("Renderer", "Using Error Material after material GPU preparation failed");
            item.pipeline = item.fallbackPipeline;
            item.material = item.fallbackMaterial;
            item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.fallbackMaterial);
        }
    }
    std::erase_if(drawList.items,
                  [](const DrawItem& item) { return !item.pipeline || !item.materialBindGroup; });
    const rhi::BindGroupHandle sceneBindGroup =
        FRAME_GPU_MANAGER.upload(context.frameIndex(), drawList);
    recordDrawCommands(context, sceneBindGroup, drawList);
}

void MiniForwardPipeline::recordDrawCommands(RenderContext& context,
                                             rhi::BindGroupHandle sceneBindGroup,
                                             const DrawList& drawList) {
    const rhi::TextureHandle backBuffer = context.swapchain().currentTexture();
    RenderGraph graph;
    graph.importTexture({
        .texture = backBuffer,
        .initialState = context.swapchain().currentTextureState(),
        .finalState = rhi::ResourceState::Present,
        .aspect = rhi::TextureAspect::Color,
    });
    RenderTarget& forwardTarget = context.currentForwardTarget();
    forwardTarget.importDepth(graph);
    rhi::RenderingInfo rendering = forwardTarget.renderingInfo();
    rendering.colorAttachments.push_back({context.swapchain().currentTextureView(),
                                          rhi::LoadOp::Clear,
                                          rhi::StoreOp::Store,
                                          drawList.clearColor});
    std::vector<RenderGraph::ResourceUsage> resources = forwardTarget.writeUsages();
    resources.insert(resources.begin(),
                     {backBuffer, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment});
    graph.addGraphicsPass("Forward",
                          std::move(rendering),
                          std::move(resources),
                          [&context, sceneBindGroup, &drawList](rhi::IGraphicsCommandEncoder& encoder) {
                              encoder.setViewport({0.0F,
                                                   0.0F,
                                                   static_cast<float>(context.swapchain().width()),
                                                   static_cast<float>(context.swapchain().height()),
                                                   0.0F,
                                                   1.0F});
                              encoder.setScissor({0, 0, context.swapchain().width(), context.swapchain().height()});
                              rhi::GraphicsPipelineHandle boundPipeline;
                              rhi::BindGroupHandle boundMaterial;
                              for (const DrawItem& item : drawList.items) {
                                  if (item.pipeline != boundPipeline) {
                                      encoder.bindPipeline(item.pipeline);
                                      encoder.bindGroup(0, sceneBindGroup);
                                      boundPipeline = item.pipeline;
                                  }
                                  if (item.materialBindGroup != boundMaterial) {
                                      encoder.bindGroup(1, item.materialBindGroup);
                                      boundMaterial = item.materialBindGroup;
                                  }
                                  for (const DrawItem::VertexBuffer& vertex : item.vertexBuffers) {
                                      encoder.bindVertexBuffer(vertex.binding, vertex.buffer);
                                  }
                                  encoder.bindIndexBuffer(item.indexBuffer, 0, item.indexFormat);
                                  encoder.drawIndexed(item.arguments);
                              }
                          });
    graph.execute(context.encoder());
}

void MiniForwardPipeline::onSwapchainChanged() {
    // Forward targets remain owned by Renderer in stage A.
}

} // namespace engine
