#include "render/gpu/texture/TextureStorageFactory.h"

#include "core/logging/Log.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine {
namespace {

[[nodiscard]] rhi::TextureType toRhi(TextureType type) {
    switch (type) {
    case TextureType::Texture2D: return rhi::TextureType::Texture2D;
    case TextureType::Texture2DArray: return rhi::TextureType::Texture2DArray;
    case TextureType::Texture3D: return rhi::TextureType::Texture3D;
    case TextureType::TextureCube: return rhi::TextureType::TextureCube;
    case TextureType::TextureCubeArray: return rhi::TextureType::TextureCubeArray;
    }
    Log::fatal("TextureStorageFactory", "Unsupported Texture type");
}

[[nodiscard]] rhi::PixelFormat toRhi(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return rhi::PixelFormat::Rgba8Unorm;
    case TextureFormat::Rgba8Srgb: return rhi::PixelFormat::Rgba8Srgb;
    }
    Log::fatal("TextureStorageFactory", "Unsupported Texture format");
}

[[nodiscard]] rhi::SamplerDesc toRhi(const TextureSamplerSettings& settings) {
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

} // namespace

bool TextureStorageFactory::create(const TextureStorageCreateInfo& request,
                                   TextureStorageEntry& destination) {
    Texture& source = request.texture;
    if (!validateTexture(source.desc(), source.pixels()))
        return false;

    const TextureDesc& desc = source.desc();
    TextureMipLayout layout;
    if (!computeTextureLayout(desc, layout))
        return false;
    const rhi::TextureDesc deviceDesc{.dimension = toRhi(desc.type),
                                      .format = toRhi(desc.format),
                                      .width = desc.width,
                                      .height = desc.height,
                                      .depth = desc.depth,
                                      .arrayLayers = desc.arrayLayers,
                                      .mipCount = desc.mipCount,
                                      .usage = rhi::TextureUsage::Sampled |
                                               rhi::TextureUsage::TransferDestination,
                                      .debugName = source.assetPath().string()};
    TextureStorageEntry created;
    created.texture = device_.createTexture(deviceDesc);
    if (!created.texture)
        return false;

    // Move the CPU blob out and slice it into per-mip regions that borrow its memory. The
    // blob dies when this function returns, so no texel data is retained on the CPU.
    const std::vector<std::uint8_t> pixels = source.takePixelData();
    std::vector<rhi::TextureUploadRegion> uploads;
    uploads.reserve(layout.offsets.size());
    std::uint32_t mipWidth = desc.width;
    std::uint32_t mipHeight = desc.height;
    for (std::uint32_t level = 0; level < layout.offsets.size(); ++level) {
        const std::size_t begin = layout.offsets[level];
        const std::size_t end = level + 1U < layout.offsets.size() ? layout.offsets[level + 1U]
                                                                   : layout.totalSize;
        const std::span<const std::byte> data{
            reinterpret_cast<const std::byte*>(pixels.data() + begin), end - begin};
        uploads.push_back({level, 0, mipWidth, mipHeight, data});
        mipWidth = std::max(1U, mipWidth / 2U);
        mipHeight = std::max(1U, mipHeight / 2U);
    }
    device_.uploadTexture(created.texture, uploads);

    const rhi::TextureViewDesc viewDesc{.type = toRhi(desc.type),
                                        .format = toRhi(desc.format),
                                        .baseMip = 0,
                                        .mipCount = desc.mipCount,
                                        .baseLayer = 0,
                                        .layerCount = desc.arrayLayers};
    created.defaultView =
        TextureView{created.texture, device_.defaultTextureView(created.texture), viewDesc};
    created.defaultSampler = Sampler::resolve(device_, toRhi(desc.sampler));
    if (!created.defaultView || !created.defaultSampler) {
        release(created);
        return false;
    }

    release(destination);
    destination = std::move(created);
    return true;
}

void TextureStorageFactory::release(TextureStorageEntry& resource) {
    if (resource.texture)
        device_.destroyTexture(resource.texture);
    resource = {};
}

} // namespace engine
