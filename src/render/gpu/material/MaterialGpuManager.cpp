#include "render/gpu/material/MaterialGpuManager.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/gpu/material/MaterialGpuFactory.h"
#include "render/material/MaterialManager.h"
#include "render/texture/TextureManager.h"
#include "render/shader/Shader.h"

#include <span>
#include <string_view>
#include <vector>

namespace engine {

MaterialGpuManager::MaterialGpuManager() = default;
MaterialGpuManager::~MaterialGpuManager() = default;

bool MaterialGpuManager::initialize(rhi::IDevice& device,
                                    rhi::RID materialLayout,
                                    std::uint32_t frameCount) {
    if (initialized()) {
        Log::error("MaterialGpuManager", "Manager is already initialized");
        return false;
    }
    if (!cache_.initialize(frameCount, kMaxResidentMaterials)) {
        Log::error("MaterialGpuManager", "Cannot initialize Material binding cache");
        return false;
    }
    device_ = &device;
    factory_ = std::make_unique<MaterialGpuFactory>(device, materialLayout);
    return true;
}

std::uint64_t MaterialGpuManager::cacheKey(RID handle) {
    return handleKey(handle);
}

namespace {

[[nodiscard]] bool sameTextureBindings(const std::vector<TextureBinding>& lhs,
                                       std::span<const TextureBinding> rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].view != rhs[i].view || lhs[i].sampler != rhs[i].sampler)
            return false;
    }
    return true;
}

} // namespace

rhi::RID MaterialGpuManager::resolve(RID handle) {
    const Material* material = MATERIAL_MANAGER.find(handle);
    if (!initialized() || !material)
        return {};

    // Texture hot reload keeps the CPU RID stable while replacing its RHI view.
    // Resolve the bindings before the cache fast path so a changed view invalidates the bind group.
    if (!collectTextureBindings(*material))
        return {};

    MaterialBindingCacheSlot slot = cache_.acquire(cacheKey(handle));
    if (slot.cacheHit) {
        MaterialGpuResource& resource = *slot.resource;
        // Fast path: both material data and the resolved TextureView+Sampler signature are stable.
        if (!resource.pendingRelease && resource.bindGroup &&
            resource.uniformVersion == material->version() &&
            sameTextureBindings(resource.textureBindings, textureScratch_)) {
            return resource.bindGroup;
        }

        // Dirty path: only the uniform payload changed, keep the bind group.
        if (!resource.pendingRelease && resource.bindGroup && resource.uniformBuffer &&
            sameTextureBindings(resource.textureBindings, textureScratch_) &&
            resource.boundSize >= material->uniformBytes().size()) {
            return factory_->updateUniforms(*material, resource) ? resource.bindGroup
                                                                 : rhi::RID{};
        }

        // Full rebuild: textures or buffer geometry changed, or the slot was evicted.
        return factory_->create({*material, textureScratch_}, resource) ? resource.bindGroup
                                                                        : rhi::RID{};
    }

    return factory_->create({*material, textureScratch_}, *slot.resource) ? slot.resource->bindGroup
                                                                          : rhi::RID{};
}

bool MaterialGpuManager::collectTextureBindings(const Material& material) {
    textureScratch_.clear();
    for (const ShaderPropertyDesc& property : material.shader().properties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        if (const TextureBinding* binding = material.getTextureBinding(property.name)) {
            textureScratch_.push_back(*binding);
            continue;
        }
        const auto found = material.textures.find(property.name);
        const std::string_view reference =
            found == material.textures.end() ? std::string_view{} : found->second;
        const RID textureHandle = TEXTURE_MANAGER.resolveReference(reference);
        const Texture* texture = TEXTURE_MANAGER.find(textureHandle);
        if (!texture) {
            Log::error(
                "MaterialGpuManager", "Material Texture is unavailable: %s", property.name.c_str());
            return false;
        }
        const Sampler sampler = Sampler::resolve(*device_, texture->defaultSamplerDesc());
        if (!sampler)
            return false;
        textureScratch_.push_back({texture->defaultView(), sampler});
    }
    return true;
}

void MaterialGpuManager::beginFrame(std::uint32_t frameIndex) {
    if (initialized())
        cache_.beginFrame(frameIndex);
}

void MaterialGpuManager::shutdown() {
    if (!initialized())
        return;
    for (MaterialGpuResource& resource : cache_.extractAll())
        factory_->release(resource);
    cache_.reset();
    factory_.reset();
    device_ = nullptr;
}

} // namespace engine
