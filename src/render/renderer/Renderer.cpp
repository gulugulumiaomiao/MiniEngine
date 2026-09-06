#include "render/renderer/Renderer.h"

#include "asset/importer/AssetImportPipeline.h"
#include "core/logging/Log.h"
#include "render/backend/IRenderBackend.h"
#include "render/mesh/Mesh.h"
#include "render/renderer/RenderScene.h"
#include "render/backend/vulkan/VulkanBackend.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace engine {
namespace {

ShaderPassType passTypeForPhase(RenderPhase phase) {
    switch (phase) {
    case RenderPhase::Forward:
        return ShaderPassType::Forward;
    case RenderPhase::DepthOnly:
        return ShaderPassType::DepthOnly;
    case RenderPhase::ShadowCaster:
        return ShaderPassType::ShadowCaster;
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

Renderer::Renderer(Window& window, bool vsync)
    : backend_(std::make_unique<VulkanBackend>(window, vsync)) {}
Renderer::~Renderer() {
    backend_->waitIdle();
}

MeshHandle Renderer::createMesh(const MeshDesc& desc, const MeshData& data) {
    return MESH_MANAGER.insert(Mesh{desc, data});
}

MeshHandle Renderer::loadMesh(const VirtualPath& meshPath) {
    return MESH_MANAGER.load(meshPath);
}

MaterialHandle Renderer::loadMaterial(const VirtualPath& materialPath) {
    return MATERIAL_MANAGER.load(materialPath);
}

void Renderer::destroyMesh(MeshHandle handle) {
    backend_->releaseMesh(handle);
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

void Renderer::setMaterialVec2(MaterialHandle handle, std::string_view name,
                               const math::Vec2& value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setVec2(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialVec3(MaterialHandle handle, std::string_view name,
                               const math::Vec3& value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setVec3(name, value);
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialVec4(MaterialHandle handle, std::string_view name,
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

void Renderer::setMaterialTexture(MaterialHandle handle, std::string_view name,
                                  std::string value) {
    if (Material* material = MATERIAL_MANAGER.find(handle)) {
        material->setTexture(name, std::move(value));
    } else {
        Log::error("Renderer", "Cannot set property on an invalid Material");
    }
}

void Renderer::setMaterialShader(MaterialHandle handle,
                                 const VirtualPath& shaderPath) {
    MATERIAL_MANAGER.setShader(handle, shaderPath);
}

void Renderer::renderFrame(const RenderScene& scene) {
    ASSET_IMPORT_PIPELINE.processFileEvents();
    constexpr std::array phases{RenderPhase::ShadowCaster,
                                RenderPhase::DepthOnly,
                                RenderPhase::Forward};
    DrawList drawList;
    if (scene.camera()) {
        const RenderCamera& camera = *scene.camera();
        drawList.scene.viewProjection = camera.projection * camera.view;
        drawList.scene.cameraPosition = math::Vec4{camera.worldPosition, 1.0F};
        drawList.clearColor = camera.clearColor;
    }
    const auto directional = std::ranges::find_if(
        scene.lights(), [](const RenderLight& light) {
            return light.type == LightType::Directional;
        });
    if (directional != scene.lights().end()) {
        drawList.scene.directionalLightDirection =
            math::Vec4{directional->direction, 1.0F};
        drawList.scene.directionalLightColorIntensity =
            math::Vec4{directional->color, directional->intensity};
    }
    drawList.objects.reserve(scene.objects().size());
    for (const RenderObject& object : scene.objects()) {
        if (scene.camera() &&
            (object.layerMask & scene.camera()->cullingMask) == 0) {
            continue;
        }
        Mesh* meshInstance = MESH_MANAGER.find(object.mesh);
        if (!meshInstance) {
            Log::warn("Renderer", "Skipping object with an invalid MeshHandle");
            continue;
        }
        const MeshDrawInfo mesh = backend_->prepareMesh(object.mesh, *meshInstance);
        if (mesh.subMeshes.empty()) {
            Log::warn("Renderer", "Skipping Mesh without GPU draw data");
            continue;
        }
        const std::uint32_t objectIndex =
            static_cast<std::uint32_t>(drawList.objects.size());
        drawList.objects.push_back({object.transform});
        for (const MeshDrawInfo::Range& range : mesh.subMeshes) {
            const MaterialHandle materialHandle =
                object.material(range.materialSlot);
            const Material* material = MATERIAL_MANAGER.find(materialHandle);
            if (!material) {
                Log::warn("Renderer",
                          "Skipping SubMesh with an invalid Material slot");
                continue;
            }
            const SubShader* subShader =
                material->shader().selectSubShader("MiniForward");
            if (!subShader) {
                Log::error("Renderer", "Shader has no MiniForward SubShader: %s",
                           material->shader().name().c_str());
                continue;
            }
            for (const RenderPhase renderPhase : phases) {
                if (renderPhase == RenderPhase::ShadowCaster &&
                    !object.castShadow) {
                    continue;
                }
                const ShaderPass* shaderPass =
                    subShader->findPass(passTypeForPhase(renderPhase));
                if (!shaderPass) continue;
                const ShaderVariantKey variant =
                    shaderPass->variantKey(material->keywords);
                const rhi::GraphicsPipelineHandle pipeline =
                    backend_->pipelineForPass(material->shader(), *shaderPass,
                                              variant,
                                              meshInstance->desc().vertexLayout);
                if (!pipeline) {
                    Log::error(
                        "Renderer", "Skipping pass without a valid pipeline: %s",
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
    std::ranges::stable_sort(drawList.items, [](const DrawItem& left,
                                                const DrawItem& right) {
        if (left.renderPhase != right.renderPhase) {
            return phaseOrder(left.renderPhase) < phaseOrder(right.renderPhase);
        }
        return left.renderQueue < right.renderQueue;
    });
    backend_->renderFrame(drawList);
}

void Renderer::waitIdle() {
    backend_->waitIdle();
}

} // namespace engine
