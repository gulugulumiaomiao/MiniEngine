#include "render/texture/Sampler.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"

namespace engine {

Sampler::Sampler(const SamplerDesc& desc, rhi::RID handle)
    : handle_(handle), filterMode_(desc.filterMode), addressModeU_(desc.addressModeU),
      addressModeV_(desc.addressModeV), maxAnisotropy_(desc.maxAnisotropy) {}

Ref<Sampler> Sampler::resolve(const SamplerDesc& desc) {
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device) {
        Log::error("Sampler", "No active device for sampler creation");
        return {};
    }
    // 层2 语义 desc 翻译成层3 RHI desc 再创建（设备按 rhi::SamplerDesc 去重）。
    const rhi::RID handle = device->createSampler(toRhi(desc));
    if (!handle) {
        Log::error("Sampler", "Failed to create sampler");
        return {};
    }
    return Ref<Sampler>(new Sampler(desc, handle));
}

} // namespace engine
