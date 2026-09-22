#include "render/gpu/shader/ShaderStorageFactory.h"

#include "rhi/api/Device.h"

#include <string>

namespace engine {

bool ShaderStorageFactory::create(const CompiledShader& description,
                                  ShaderStorageEntry& destination) {
    ShaderStorageEntry created;
    created.shader = device_.createShader({
        .stage = description.stage == ShaderStage::Vertex ? rhi::ShaderStage::Vertex
                                                          : rhi::ShaderStage::Fragment,
        .bytecode = description.bytecode,
        .debugName = "CompiledShader/" + std::to_string(description.id),
    });
    if (!created.shader)
        return false;
    release(destination);
    destination = created;
    return true;
}

void ShaderStorageFactory::release(ShaderStorageEntry& resource) {
    if (resource.shader)
        device_.destroyShader(resource.shader);
    resource = {};
}

} // namespace engine
