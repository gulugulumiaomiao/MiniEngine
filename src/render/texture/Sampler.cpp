#include "render/texture/Sampler.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

namespace engine {

Sampler::Sampler(const rhi::SamplerDesc& desc, rhi::RID handle)
    : handle_(handle), minFilter_(desc.minFilter), magFilter_(desc.magFilter),
      mipmapFilter_(desc.mipmapFilter), addressU_(desc.addressU), addressV_(desc.addressV),
      maxAnisotropy_(desc.maxAnisotropy) {}

Ref<Sampler> Sampler::resolve(const rhi::SamplerDesc& desc) {
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device) {
        Log::error("Sampler", "No active device for sampler creation");
        return {};
    }
    const rhi::RID handle = device->createSampler(desc);
    if (!handle) {
        Log::error("Sampler", "Failed to create sampler");
        return {};
    }
    return Ref<Sampler>(new Sampler(desc, handle));
}

} // namespace engine
