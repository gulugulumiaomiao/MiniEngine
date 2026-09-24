#include "render/gpu/material/MaterialStorage.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/gpu/material/MaterialStorageFactory.h"
#include "render/gpu/pipeline/GraphicsPipelineStorage.h"
#include "render/material/MaterialManager.h"
#include "render/texture/Sampler.h"
#include "render/shader/Shader.h"
#include "rhi/api/Device.h"

#include <span>
#include <string_view>
#include <vector>

namespace engine {

MaterialStorage::MaterialStorage() = default;
MaterialStorage::~MaterialStorage() = default;

bool MaterialStorage::initialize(rhi::IDevice& device,
                                 rhi::RID materialLayout,
                                 std::uint32_t frameCount) {
    if (initialized()) {
        Log::error("MaterialStorage", "Manager is already initialized");
        return false;
    }
    if (!cache_.initialize(frameCount, kMaxResidentMaterials)) {
        Log::error("MaterialStorage", "Cannot initialize Material binding cache");
        return false;
    }
    device_ = &device;
    factory_ = std::make_unique<MaterialStorageFactory>(device, materialLayout);
    MATERIAL_RESOURCE_MANAGER.setDestroyObserver([this](RID handle) { invalidate(handle); });
    return true;
}

std::uint64_t MaterialStorage::cacheKey(RID handle) {
    return handleKey(handle);
}

namespace {

[[nodiscard]] bool sameTextureBindings(const std::vector<rhi::TextureBinding>& lhs,
                                       std::span<const rhi::TextureBinding> rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].view != rhs[i].view || lhs[i].sampler != rhs[i].sampler)
            return false;
    }
    return true;
}

} // namespace

MaterialPassState MaterialStorage::resolvePass(const Material& material,
                                               const Mesh& mesh,
                                               ShaderPassType passType,
                                               std::string_view renderPipeline,
                                               rhi::PixelFormat colorFormat,
                                               rhi::PixelFormat depthFormat) {
    const SubShader* subShader = material.shader().selectSubShader(renderPipeline);
    if (!subShader)
        return {};
    const ShaderPass* pass = subShader->findPass(passType);
    if (!pass)
        return {};
    const ShaderVariantKey variant = pass->variantKey(material.keywords);
    return {pass,
            GRAPHICS_PIPELINE_STORAGE.resolve(
                material.shader(), *pass, variant, mesh, colorFormat, depthFormat)};
}

rhi::RID MaterialStorage::resolve(Material& material) {
    if (!initialized() || !material.resourceId())
        return {};
    const RID handle = material.resourceId();

    // Texture hot reload keeps the CPU RID stable while replacing its RHI view.
    // Resolve the bindings before the cache fast path so a changed view invalidates the bind group.
    if (!collectTextureBindings(material))
        return {};

    MaterialStorageCacheSlot slot = cache_.acquire(cacheKey(handle));
    if (slot.cacheHit) {
        MaterialStorageEntry& resource = *slot.resource;
        // Fast path: both material data and the resolved TextureView+Sampler signature are stable.
        if (!resource.pendingRelease && resource.bindGroup &&
            resource.uniformVersion == material.version() &&
            sameTextureBindings(resource.textureBindings, textureScratch_)) {
            return resource.bindGroup;
        }

        // Dirty path: only the uniform payload changed, keep the bind group.
        if (!resource.pendingRelease && resource.bindGroup && resource.uniformBuffer &&
            sameTextureBindings(resource.textureBindings, textureScratch_) &&
            resource.boundSize >= material.uniformBytes().size()) {
            return factory_->updateUniforms(material, resource) ? resource.bindGroup
                                                                 : rhi::RID{};
        }

        // Full rebuild: textures or buffer geometry changed, or the slot was evicted.
        return factory_->create({material, textureScratch_}, resource) ? resource.bindGroup
                                                                        : rhi::RID{};
    }

    return factory_->create({material, textureScratch_}, *slot.resource) ? slot.resource->bindGroup
                                                                          : rhi::RID{};
}

bool MaterialStorage::collectTextureBindings(const Material& material) {
    textureScratch_.clear();
    for (const ShaderPropertyDesc& property : material.shader().properties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const Ref<Texture> texture = material.resolveTexture(property.name);
        const Ref<Sampler> sampler = material.resolveSampler(property.name);
        const rhi::TextureBinding resolved =
            texture ? (sampler ? texture->binding(sampler) : texture->binding())
                    : rhi::TextureBinding{};
        if (!resolved.view || !resolved.sampler) {
            Log::error(
                "MaterialStorage", "Material Texture is unavailable: %s", property.name.c_str());
            return false;
        }
        textureScratch_.push_back(resolved);
    }
    return true;
}

void MaterialStorage::invalidate(RID handle) {
    if (!initialized())
        return;
    std::vector<MaterialStorageEntry> resources = cache_.extract(cacheKey(handle));
    if (resources.empty())
        return;
    device_->waitIdle();
    for (MaterialStorageEntry& resource : resources)
        factory_->release(resource);
}

void MaterialStorage::beginFrame(std::uint32_t frameIndex) {
    if (initialized())
        cache_.beginFrame(frameIndex);
}

void MaterialStorage::shutdown() {
    MATERIAL_RESOURCE_MANAGER.setDestroyObserver({});
    if (!initialized())
        return;
    for (MaterialStorageEntry& resource : cache_.extractAll())
        factory_->release(resource);
    cache_.reset();
    factory_.reset();
    device_ = nullptr;
}

} // namespace engine
