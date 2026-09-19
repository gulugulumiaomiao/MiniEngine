#include "render/renderer/DrawListBuilder.h"

#include "core/logging/Log.h"
#include "core/math/Frustum.h"
#include "render/gpu/mesh/MeshGpuManager.h"
#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/MeshManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/render_target/RenderTarget.h"
#include "render/scene/RenderScene.h"
#include "render/shader/Shader.h"

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

void extractLighting(const RenderScene& scene, SourceDrawData& result) {
    const auto directional = std::ranges::find_if(scene.lights(), [](const RenderLight& light) {
        return light.type == LightType::Directional;
    });
    if (directional != scene.lights().end()) {
        result.scene.directionalLightDirection = math::Vec4{directional->direction, 1.0F};
        result.scene.directionalLightColorIntensity =
            math::Vec4{directional->color, directional->intensity};
    }
    if (directional != scene.lights().end() && directional->castShadow &&
        !scene.objects().empty()) {
        math::Vec3 minimum{std::numeric_limits<float>::max()};
        math::Vec3 maximum{std::numeric_limits<float>::lowest()};
        for (const RenderObject& object : scene.objects()) {
            const math::Vec3 center = math::transformPoint(object.transform, math::Vec3{0.0F});
            const math::Vec3 extent{object.boundsRadius};
            minimum = math::min(minimum, center - extent);
            maximum = math::max(maximum, center + extent);
        }
        const math::Vec3 center = (minimum + maximum) * 0.5F;
        const float radius = math::length(maximum - minimum) * 0.5F;
        math::Vec3 up{0.0F, 1.0F, 0.0F};
        if (std::abs(math::dot(directional->direction, up)) > 0.9F) {
            up = math::Vec3{1.0F, 0.0F, 0.0F};
        }
        const float distance = radius * 2.0F + 1.0F;
        result.scene.lightSpaceMatrix =
            math::orthographic(-radius, radius, -radius, radius, 0.1F, distance + radius * 2.0F) *
            math::lookAt(center - directional->direction * distance, center, up);
        result.scene.shadowParams = math::Vec4{0.8F, 0.0025F, 0.05F, 1.0F / 1024.0F};
    }
    const auto point = std::ranges::find_if(
        scene.lights(), [](const RenderLight& light) { return light.type == LightType::Point; });
    if (point != scene.lights().end()) {
        result.scene.pointLightPositionRange = math::Vec4{point->position, point->range};
        result.scene.pointLightColorIntensity = math::Vec4{point->color, point->intensity};
    }
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
    if (!material) {
        return {};
    }
    const SubShader* subShader = material->shader().selectSubShader(renderPipeline_);
    if (!subShader) {
        return {};
    }
    const ShaderPass* shaderPass = subShader->findPass(passTypeForPhase(phase));
    if (!shaderPass) {
        return {};
    }
    const ShaderVariantKey variant = shaderPass->variantKey(material->keywords);
    const bool depthOnly = phase != RenderPhase::Forward;
    const rhi::TextureFormat colorFormat =
        depthOnly ? rhi::TextureFormat::Undefined : context.sceneColorFormat();
    const rhi::TextureFormat depthFormat = phase == RenderPhase::ShadowCaster
                                               ? rhi::TextureFormat::Depth32Float
                                               : context.currentForwardTarget().depthFormat();
    return {materialHandle,
            shaderPass,
            GRAPHICS_PIPELINE_MANAGER.resolve(
                material->shader(), *shaderPass, variant, meshInstance, colorFormat, depthFormat)};
}

SourceDrawData DrawListBuilder::extract(const RenderScene& scene,
                                        const RenderContext& context) const {
    SourceDrawData result;
    if (context.offscreenScene() && !scene.camera()) {
        return result;
    }
    std::optional<math::Frustum> frustum;
    if (scene.camera()) {
        const RenderCamera& camera = *scene.camera();
        result.scene.viewProjection = camera.projection * camera.view;
        result.scene.cameraPosition = math::Vec4{camera.worldPosition, 1.0F};
        result.clearColor = camera.clearColor;
        frustum = math::extractFrustum(result.scene.viewProjection);
    }
    extractLighting(scene, result);

    result.objects.reserve(scene.objects().size());
    for (std::size_t sceneObjectIndex = 0; sceneObjectIndex < scene.objects().size();
         ++sceneObjectIndex) {
        const RenderObject& object = scene.objects()[sceneObjectIndex];
        if (scene.camera() && (object.layerMask & scene.camera()->cullingMask) == 0) {
            continue;
        }
        const math::Vec3 center = math::transformPoint(object.transform, math::Vec3{0.0F});
        if (frustum && !math::intersects(*frustum, center, object.boundsRadius)) {
            continue;
        }
        Mesh* mesh = MESH_MANAGER.find(object.mesh);
        if (!mesh) {
            Log::warn("DrawListBuilder", "Skipping object with an invalid MeshHandle");
            continue;
        }
        const MeshDrawInfo gpu = MESH_GPU_MANAGER.resolve(object.mesh);
        if (gpu.subMeshes.empty()) {
            Log::warn("DrawListBuilder", "Skipping Mesh without GPU draw data");
            continue;
        }
        const std::uint32_t objectIndex = static_cast<std::uint32_t>(result.objects.size());
        result.objects.push_back({object.transform});
        for (std::size_t subMeshIndex = 0; subMeshIndex < gpu.subMeshes.size(); ++subMeshIndex) {
            const MeshDrawInfo::Range& range = gpu.subMeshes[subMeshIndex];
            SourceDrawItem item;
            item.mesh = object.mesh;
            item.subMeshIndex = static_cast<std::uint32_t>(subMeshIndex);
            item.vertexLayout = mesh->desc().vertexLayout;
            item.vertexBuffers.reserve(gpu.vertexBuffers.size());
            for (const DrawItem::VertexBuffer& vertex : gpu.vertexBuffers) {
                item.vertexBuffers.push_back({vertex.binding, vertex.buffer, 0});
            }
            item.indexBuffer = gpu.indexBuffer;
            item.indexRange = {range.firstIndex, range.indexCount, range.vertexOffset};
            item.material = object.material(range.materialSlot);
            item.worldMatrix = object.transform;
            item.worldBounds = mesh->desc().bounds;
            item.layerMask = object.layerMask;
            item.objectId = sceneObjectIndex;
            item.objectIndex = objectIndex;
            item.castShadow = object.castShadow;
            result.items.push_back(std::move(item));
        }
    }
    return result;
}

DrawList DrawListBuilder::prepare(const SourceDrawData& source,
                                  const RenderContext& context) const {
    constexpr std::array phases{
        RenderPhase::ShadowCaster, RenderPhase::DepthOnly, RenderPhase::Forward};
    DrawList result;
    result.objects = source.objects;
    result.scene = source.scene;
    result.clearColor = source.clearColor;
    for (const SourceDrawItem& sourceItem : source.items) {
        Mesh* mesh = MESH_MANAGER.find(sourceItem.mesh);
        if (!mesh) {
            continue;
        }
        bool acceptedByPipeline = false;
        for (const RenderPhase phase : phases) {
            if (phase == RenderPhase::ShadowCaster && !sourceItem.castShadow) {
                continue;
            }
            ResolvedMaterialPass resolved =
                resolveMaterialPass(context, sourceItem.material, *mesh, phase);
            ResolvedMaterialPass fallback;
            if (phase == RenderPhase::Forward) {
                fallback = resolveMaterialPass(
                    context, MATERIAL_MANAGER.errorMaterial(), *mesh, phase);
                if (!resolved) {
                    Log::error("DrawListBuilder",
                               "Using Error Material for an unavailable material");
                    resolved = fallback;
                }
            }
            if (!resolved) {
                continue;
            }
            acceptedByPipeline = true;
            const Material* material = MATERIAL_MANAGER.find(resolved.material);
            DrawItem item;
            item.shaderPass = resolved.pass;
            item.renderPhase = phase;
            item.mesh = sourceItem.mesh;
            item.pipeline = resolved.pipeline;
            item.drawState = GraphicsPipelineManager::makeDrawState(*resolved.pass);
            item.drawState.colorAttachmentCount = phase == RenderPhase::Forward ? 1u : 0u;
            item.material = resolved.material;
            item.fallbackPipeline = fallback.pipeline;
            item.fallbackMaterial = fallback.material;
            item.vertexBuffers.reserve(sourceItem.vertexBuffers.size());
            for (const SourceDrawItem::VertexBuffer& vertex : sourceItem.vertexBuffers) {
                item.vertexBuffers.push_back({vertex.binding, vertex.buffer});
            }
            item.indexBuffer = sourceItem.indexBuffer;
            item.indexFormat = rhi::IndexFormat::UInt32;
            item.arguments = {.indexCount = sourceItem.indexRange.indexCount,
                              .instanceCount = 1,
                              .firstIndex = sourceItem.indexRange.firstIndex,
                              .vertexOffset = sourceItem.indexRange.vertexOffset,
                              .firstInstance = sourceItem.objectIndex};
            item.renderQueue = material->renderQueue;
            item.layerMask = sourceItem.layerMask;
            result.items.push_back(item);
            result.groups[item.renderQueue].push_back(std::move(item));
        }
        if (acceptedByPipeline) {
            const Material* material = MATERIAL_MANAGER.find(sourceItem.material);
            const int queue = material ? material->renderQueue : 2000;
            result.sourceGroups[queue].push_back(sourceItem);
        }
    }
    return result;
}

DrawList DrawListBuilder::build(const RenderScene& scene, const RenderContext& context) {
    return prepare(extract(scene, context), context);
}

} // namespace engine
