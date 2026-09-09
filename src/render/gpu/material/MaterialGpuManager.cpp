#include "render/gpu/material/MaterialGpuManager.h"

#include "core/logging/Log.h"
#include "render/gpu/material/MaterialGpuFactory.h"
#include "render/gpu/texture/TextureGpuManager.h"
#include "render/material/MaterialManager.h"
#include "render/shader/Shader.h"

#include <string_view>
#include <vector>

namespace engine {

MaterialGpuManager::MaterialGpuManager() = default;
MaterialGpuManager::~MaterialGpuManager() = default;

bool MaterialGpuManager::initialize(rhi::IDevice& device,
                                    rhi::BindGroupLayoutHandle materialLayout,
                                    std::uint32_t frameCount) {
    if (initialized()) {
        Log::error("MaterialGpuManager", "Manager is already initialized");
        return false;
    }
    if (!cache_.initialize(frameCount)) {
        Log::error("MaterialGpuManager", "Cannot initialize Material binding cache");
        return false;
    }
    factory_ = std::make_unique<MaterialGpuFactory>(device, materialLayout);
    return true;
}

std::uint64_t MaterialGpuManager::cacheKey(MaterialHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

rhi::BindGroupHandle MaterialGpuManager::resolve(MaterialHandle handle) {
    const Material* material = MATERIAL_MANAGER.find(handle);
    if (!initialized() || !material)
        return {};

    MaterialBindingCacheSlot slot = cache_.acquire(cacheKey(handle));
    if (slot.cacheHit)
        return slot.resource->bindGroup;

    std::vector<rhi::TextureBinding> textures;
    for (const ShaderPropertyDesc& property : material->shader().properties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const auto found = material->textures.find(property.name);
        const std::string_view reference =
            found == material->textures.end() ? std::string_view{} : found->second;
        const auto texture = TEXTURE_GPU_MANAGER.resolveReference(reference);
        if (!texture) {
            Log::error(
                "MaterialGpuManager", "Material Texture is unavailable: %s", property.name.c_str());
            return {};
        }
        textures.push_back(*texture);
    }
    return factory_->create({*material, textures}, *slot.resource) ? slot.resource->bindGroup
                                                                   : rhi::BindGroupHandle{};
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
}

} // namespace engine
