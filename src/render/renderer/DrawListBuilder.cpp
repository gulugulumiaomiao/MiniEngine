#include "render/renderer/DrawListBuilder.h"

#include "core/logging/Log.h"
#include "core/math/Frustum.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/gpu/mesh/MeshGpuManager.h"
#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "render/render_target/RenderTarget.h"
#include "render/scene/RenderScene.h"
#include "render/shader/Shader.h"
#include "rhi/api/Swapchain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>

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

} // namespace

DrawListBuilder::DrawListBuilder(std::string_view renderPipeline)
    : renderPipeline_(renderPipeline) {}

DrawListBuilder::ResolvedMaterialPass DrawListBuilder::resolveMaterialPass(
    const RenderContext& context,
    MaterialHandle materialHandle,
    const Mesh& meshInstance,
    RenderPhase phase) const {
    const Material* material = MATERIAL_MANAGER.find(materialHandle);
    if (!material)
        return {};
    const SubShader* subShader = material->shader().selectSubShader(renderPipeline_);
    if (!subShader)
        return {};
    const ShaderPass* shaderPass = subShader->findPass(passTypeForPhase(phase));
    if (!shaderPass)
        return {};
    const ShaderVariantKey variant = shaderPass->variantKey(material->keywords);
    // ShadowCaster pipelines render depth-only into the off-screen shadow map: they are
    // resolved against an empty color attachment list and the shadow map depth format.
    const bool shadowCaster = phase == RenderPhase::ShadowCaster;
    const rhi::TextureFormat colorFormat =
        shadowCaster ? rhi::TextureFormat::Undefined : context.swapchain().format();
    const rhi::TextureFormat depthFormat = shadowCaster
                                               ? rhi::TextureFormat::Depth32Float
                                               : context.currentForwardTarget().depthFormat();
    return {
        materialHandle,
        shaderPass,
        GRAPHICS_PIPELINE_MANAGER.resolve(material->shader(),
                                          *shaderPass,
                                          variant,
                                          meshInstance.desc().vertexLayout,
                                          colorFormat,
                                          depthFormat),
    };
}

DrawList DrawListBuilder::build(const RenderScene& scene, const RenderContext& context) {
    constexpr std::array phases{
        RenderPhase::ShadowCaster, RenderPhase::DepthOnly, RenderPhase::Forward};

    DrawList drawList;
    std::optional<math::Frustum> frustum;
    if (scene.camera()) {
        const RenderCamera& camera = *scene.camera();
        drawList.scene.viewProjection = camera.projection * camera.view;
        drawList.scene.cameraPosition = math::Vec4{camera.worldPosition, 1.0F};
        drawList.clearColor = camera.clearColor;
        frustum = math::extractFrustum(drawList.scene.viewProjection);
    }

    const auto directional = std::ranges::find_if(scene.lights(), [](const RenderLight& light) {
        return light.type == LightType::Directional;
    });
    if (directional != scene.lights().end()) {
        drawList.scene.directionalLightDirection = math::Vec4{directional->direction, 1.0F};
        drawList.scene.directionalLightColorIntensity =
            math::Vec4{directional->color, directional->intensity};
    }
    if (directional != scene.lights().end() && directional->castShadow &&
        !scene.objects().empty()) {
        // Fit an orthographic light-space volume around a sphere covering every object's
        // position plus its bounds radius; the shadow map then covers the whole scene.
        math::Vec3 minBounds{std::numeric_limits<float>::max()};
        math::Vec3 maxBounds{std::numeric_limits<float>::lowest()};
        for (const RenderObject& object : scene.objects()) {
            const math::Vec3 center = math::transformPoint(object.transform, math::Vec3{0.0F});
            const math::Vec3 extent{object.boundsRadius};
            minBounds = math::min(minBounds, center - extent);
            maxBounds = math::max(maxBounds, center + extent);
        }
        const math::Vec3 center = (minBounds + maxBounds) * 0.5F;
        const float radius = math::length(maxBounds - minBounds) * 0.5F;
        math::Vec3 up{0.0F, 1.0F, 0.0F};
        if (std::abs(math::dot(directional->direction, up)) > 0.9F) {
            up = math::Vec3{1.0F, 0.0F, 0.0F};
        }
        const float distance = radius * 2.0F + 1.0F;
        drawList.scene.lightSpaceMatrix =
            math::lookAt(center - directional->direction * distance, center, up) *
            math::orthographic(-radius,
                               radius,
                               -radius,
                               radius,
                               0.1F,
                               distance + radius * 2.0F);
        drawList.scene.shadowParams = math::Vec4{0.8F, 0.0025F, 0.05F, 1.0F / 1024.0F};
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
        if (frustum) {
            const math::Vec3 center = math::transformPoint(object.transform, math::Vec3{0.0F});
            if (!math::intersects(*frustum, center, object.boundsRadius)) {
                continue;
            }
        }
        Mesh* meshInstance = MESH_MANAGER.find(object.mesh);
        if (!meshInstance) {
            Log::warn("DrawListBuilder", "Skipping object with an invalid MeshHandle");
            continue;
        }
        const MeshDrawInfo mesh = MESH_GPU_MANAGER.resolve(object.mesh);
        if (mesh.subMeshes.empty()) {
            Log::warn("DrawListBuilder", "Skipping Mesh without GPU draw data");
            continue;
        }
        const std::uint32_t objectIndex = static_cast<std::uint32_t>(drawList.objects.size());
        drawList.objects.push_back({object.transform});

        for (const MeshDrawInfo::Range& range : mesh.subMeshes) {
            const MaterialHandle requestedMaterial = object.material(range.materialSlot);
            for (const RenderPhase renderPhase : phases) {
                if (renderPhase == RenderPhase::ShadowCaster && !object.castShadow) {
                    continue;
                }
                ResolvedMaterialPass resolved =
                    resolveMaterialPass(context, requestedMaterial, *meshInstance, renderPhase);
                ResolvedMaterialPass fallback;
                if (renderPhase == RenderPhase::Forward) {
                    fallback = resolveMaterialPass(
                        context, MATERIAL_MANAGER.errorMaterial(), *meshInstance, renderPhase);
                    if (!resolved) {
                        Log::error("DrawListBuilder",
                                   "Using Error Material for an unavailable material");
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
                    .mesh = object.mesh,
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

    return drawList;
}

} // namespace engine
