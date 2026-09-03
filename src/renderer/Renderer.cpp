#include "renderer/Renderer.h"

#include "core/Log.h"
#include "renderer/AssetManager.h"
#include "renderer/IRenderBackend.h"
#include "renderer/RenderScene.h"
#include "rhi/vulkan/VulkanBackend.h"

#include <algorithm>
#include <array>
#include <filesystem>
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

Renderer::Renderer(Window& window)
    : backend_(std::make_unique<VulkanBackend>(window)) {
    ASSET_MANAGER.setAssetRoot(std::filesystem::path{MINI_ASSET_DIR});
}
Renderer::~Renderer() = default;

MeshHandle Renderer::createMesh(const MeshDesc& desc, const MeshData& data) {
    return backend_->createMesh(desc, data);
}

MaterialHandle Renderer::loadMaterial(const std::filesystem::path& materialPath) {
    return MATERIAL_MANAGER.createInstance(materialPath);
}

void Renderer::destroyMesh(MeshHandle handle) {
    backend_->destroyMesh(handle);
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
                                 const std::filesystem::path& shaderPath) {
    MATERIAL_MANAGER.setShader(handle, shaderPath);
}

void Renderer::renderFrame(const RenderScene& scene) {
    constexpr std::array phases{RenderPhase::ShadowCaster,
                                RenderPhase::DepthOnly,
                                RenderPhase::Forward};
    DrawList drawList;
    drawList.objects.reserve(scene.objects().size());
    for (const RenderObject& object : scene.objects()) {
        const Material* material = MATERIAL_MANAGER.find(object.material);
        if (!material) {
            Log::error("Renderer", "Skipping object with an invalid Material");
            continue;
        }
        const SubShader* subShader =
            material->shader().selectSubShader("MiniForward");
        if (!subShader) {
            Log::error("Renderer", "Shader has no MiniForward SubShader: %s",
                       material->shader().name().c_str());
            continue;
        }
        const MeshDrawInfo mesh = backend_->meshDrawInfo(object.mesh);
        const std::uint32_t objectIndex =
            static_cast<std::uint32_t>(drawList.objects.size());
        drawList.objects.push_back({object.transform});
        for (const RenderPhase renderPhase : phases) {
            const ShaderPass* shaderPass =
                subShader->findPass(passTypeForPhase(renderPhase));
            if (!shaderPass) continue;
            const ShaderVariantKey variant =
                shaderPass->variantKey(material->keywords);
            const rhi::GraphicsPipelineHandle pipeline =
                backend_->pipelineForPass(material->shader(), *shaderPass,
                                          variant);
            if (!pipeline) {
                Log::error("Renderer", "Skipping pass without a valid pipeline: %s",
                           shaderPass->name().c_str());
                continue;
            }
            for (const MeshDrawInfo::Range& range : mesh.subMeshes) {
                drawList.items.push_back({
                    .shaderPass = shaderPass,
                    .renderPhase = renderPhase,
                    .pipeline = pipeline,
                    .material = object.material,
                    .vertexBuffer = mesh.vertexBuffer,
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
