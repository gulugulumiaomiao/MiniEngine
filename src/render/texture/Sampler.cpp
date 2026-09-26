#include "render/texture/Sampler.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "rhi/api/Device.h"

namespace engine {

bool SamplerDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("filter_mode", filterMode) &&
           archive.transfer("address_mode_u", addressModeU) &&
           archive.transfer("address_mode_v", addressModeV) &&
           archive.transfer("max_anisotropy", maxAnisotropy) && archive.endObject();
}

rhi::SamplerDesc toRhi(const SamplerDesc& settings) {
    rhi::SamplerDesc desc;
    desc.maxAnisotropy = settings.maxAnisotropy;
    switch (settings.addressModeU) {
    case TextureAddressMode::Repeat: desc.addressU = rhi::SamplerAddressMode::Repeat; break;
    case TextureAddressMode::MirroredRepeat:
        desc.addressU = rhi::SamplerAddressMode::MirroredRepeat;
        break;
    case TextureAddressMode::ClampToEdge:
        desc.addressU = rhi::SamplerAddressMode::ClampToEdge;
        break;
    }
    switch (settings.addressModeV) {
    case TextureAddressMode::Repeat: desc.addressV = rhi::SamplerAddressMode::Repeat; break;
    case TextureAddressMode::MirroredRepeat:
        desc.addressV = rhi::SamplerAddressMode::MirroredRepeat;
        break;
    case TextureAddressMode::ClampToEdge:
        desc.addressV = rhi::SamplerAddressMode::ClampToEdge;
        break;
    }
    switch (settings.filterMode) {
    case TextureFilterMode::Point:
        desc.minFilter = rhi::SamplerFilter::Nearest;
        desc.magFilter = rhi::SamplerFilter::Nearest;
        desc.mipmapFilter = rhi::SamplerMipmapFilter::Nearest;
        break;
    case TextureFilterMode::Bilinear:
        desc.minFilter = rhi::SamplerFilter::Linear;
        desc.magFilter = rhi::SamplerFilter::Linear;
        desc.mipmapFilter = rhi::SamplerMipmapFilter::Nearest;
        break;
    case TextureFilterMode::Trilinear:
        desc.minFilter = rhi::SamplerFilter::Linear;
        desc.magFilter = rhi::SamplerFilter::Linear;
        desc.mipmapFilter = rhi::SamplerMipmapFilter::Linear;
        break;
    }
    return desc;
}

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
    const rhi::RID handle = device->sampler_create(toRhi(desc));
    if (!handle) {
        Log::error("Sampler", "Failed to create sampler");
        return {};
    }
    return Ref<Sampler>(new Sampler(desc, handle));
}

} // namespace engine
