#include "render/texture/Sampler.h"

#include "rhi/api/Device.h"

namespace engine {

Sampler Sampler::resolve(rhi::IDevice& device, const rhi::SamplerDesc& desc) {
    return {device.createSampler(desc), desc};
}

} // namespace engine
