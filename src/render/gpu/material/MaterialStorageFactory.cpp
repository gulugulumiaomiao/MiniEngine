#include "render/gpu/material/MaterialStorageFactory.h"

#include "render/material/Material.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <vector>

namespace engine {

bool MaterialStorageFactory::create(const MaterialStorageCreateInfo& request,
                                    MaterialStorageEntry& destination) {
    if (destination.bindGroup) {
        device_.bind_group_destroy(destination.bindGroup);
        destination.bindGroup = {};
    }

    const std::uint64_t byteSize =
        std::max<std::size_t>(16, request.material.uniformBytes().size());
    if (!destination.uniformBuffer || destination.uniformCapacity < byteSize) {
        if (destination.uniformBuffer)
            device_.buffer_destroy(destination.uniformBuffer);
        destination.uniformBuffer = device_.buffer_create({
            .size = byteSize,
            .usage = rhi::BufferUsage::Uniform,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Material uniforms",
        });
        destination.uniformCapacity = byteSize;
    }
    if (!request.material.uniformBytes().empty())
        device_.buffer_upload(destination.uniformBuffer, request.material.uniformBytes());

    std::vector<rhi::BindGroupEntry> bindings;
    bindings.reserve(request.textures.size() + 1);
    bindings.push_back({
        .binding = 0,
        .type = rhi::BindingType::UniformBuffer,
        .buffer = destination.uniformBuffer,
        .size = byteSize,
    });
    std::uint32_t binding = 1;
    for (const rhi::TextureBinding& resolved : request.textures) {
        bindings.push_back({
            .binding = binding++,
            .type = rhi::BindingType::SampledTexture,
            .textureView = resolved.view,
            .sampler = resolved.sampler,
        });
    }
    destination.bindGroup = device_.bind_group_create({layout_, bindings, "Material bind group"});
    if (!destination.bindGroup)
        return false;

    // Persistent-residency bookkeeping for the freshly built resource.
    destination.uniformVersion = request.material.version();
    destination.boundSize = byteSize;
    destination.textureBindings.assign(request.textures.begin(), request.textures.end());
    destination.pendingRelease = false;
    return true;
}

bool MaterialStorageFactory::updateUniforms(const Material& material,
                                            MaterialStorageEntry& resource) {
    if (!resource.uniformBuffer)
        return false;
    if (material.version() == resource.uniformVersion)
        return true;
    if (!material.uniformBytes().empty())
        device_.buffer_upload(resource.uniformBuffer, material.uniformBytes());
    resource.uniformVersion = material.version();
    return true;
}

void MaterialStorageFactory::release(MaterialStorageEntry& resource) {
    if (resource.bindGroup)
        device_.bind_group_destroy(resource.bindGroup);
    if (resource.uniformBuffer)
        device_.buffer_destroy(resource.uniformBuffer);
    resource = {};
}

} // namespace engine
