#include "render/gpu/shader/ShaderModuleGpuFactory.h"

#include "rhi/api/Device.h"

#include <string>

namespace engine {

bool ShaderModuleGpuFactory::create(const CompiledShader& description,
                                    ShaderModuleGpuResource& destination) {
    ShaderModuleGpuResource created;
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

void ShaderModuleGpuFactory::release(ShaderModuleGpuResource& resource) {
    if (resource.shader)
        device_.destroyShader(resource.shader);
    resource = {};
}

} // namespace engine
