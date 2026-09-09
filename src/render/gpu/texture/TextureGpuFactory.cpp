#include "render/gpu/texture/TextureGpuFactory.h"

#include "core/logging/Log.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <vector>

namespace engine {
namespace {

rhi::TextureFormat toRhi(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return rhi::TextureFormat::Rgba8Unorm;
    case TextureFormat::Rgba8Srgb: return rhi::TextureFormat::Rgba8Srgb;
    }
    Log::fatal("TextureGpuFactory", "Unsupported Texture format");
}

} // namespace

bool TextureGpuFactory::create(const TextureGpuCreateInfo& request,
                               TextureGpuResource& destination) {
    const Texture& texture = request.texture;
    if (!validateTexture(texture.desc(), texture.mipData()))
        return false;

    const TextureDesc& desc = texture.desc();
    TextureGpuResource uploaded;
    uploaded.texture = device_.createTexture({
        .dimension = rhi::TextureDimension::Texture2D,
        .format = toRhi(desc.format),
        .width = desc.width,
        .height = desc.height,
        .depth = 1,
        .mipCount = desc.mipCount,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination,
        .debugName = texture.assetPath().string(),
    });
    std::vector<rhi::TextureUploadRegion> uploads;
    uploads.reserve(texture.mipData().size());
    std::uint32_t mipLevel{};
    for (const TextureMipData& mip : texture.mipData())
        uploads.push_back({mipLevel++, 0, mip.width, mip.height, mip.bytes});
    device_.uploadTexture(uploaded.texture, uploads);
    uploaded.view = device_.createTextureView({
        .texture = uploaded.texture,
        .format = toRhi(desc.format),
        .baseMipLevel = 0,
        .mipCount = desc.mipCount,
    });
    release(destination);
    destination = uploaded;
    return true;
}

void TextureGpuFactory::release(TextureGpuResource& resource) {
    if (resource.view)
        device_.destroyTextureView(resource.view);
    if (resource.texture)
        device_.destroyTexture(resource.texture);
    resource = {};
}

} // namespace engine
