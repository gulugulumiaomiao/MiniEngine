#include "render/gpu/texture/SamplerGpuFactory.h"

#include "rhi/api/Device.h"

namespace engine {

bool SamplerGpuFactory::create(const rhi::SamplerDesc& description,
                               rhi::SamplerHandle& destination) {
    const rhi::SamplerHandle created = device_.createSampler(description);
    if (!created)
        return false;
    release(destination);
    destination = created;
    return true;
}

void SamplerGpuFactory::release(rhi::SamplerHandle& resource) {
    if (resource)
        device_.destroySampler(resource);
    resource = {};
}

} // namespace engine
