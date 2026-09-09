#include "render/cache/TextureGpuCache.h"

#include "core/logging/Log.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <string>
#include <vector>

namespace engine {
namespace {

rhi::TextureFormat toRhi(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return rhi::TextureFormat::Rgba8Unorm;
    case TextureFormat::Rgba8Srgb: return rhi::TextureFormat::Rgba8Srgb;
    }
    Log::fatal("TextureGpuCache", "Unsupported Texture format");
}

} // namespace

TextureGpuCache::TextureGpuCache(rhi::IDevice& device) : device_(device) {
    sampler_ = device_.createSampler({
        .minFilter = rhi::SamplerFilter::Linear,
        .magFilter = rhi::SamplerFilter::Linear,
        .mipmapFilter = rhi::SamplerMipmapFilter::Linear,
        .addressU = rhi::SamplerAddressMode::Repeat,
        .addressV = rhi::SamplerAddressMode::Repeat,
        .maxAnisotropy = 1.0F,
    });
}

TextureGpuCache::~TextureGpuCache() {
    clear();
    if (sampler_)
        device_.destroySampler(sampler_);
}

std::uint64_t TextureGpuCache::key(TextureHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

void TextureGpuCache::destroy(Entry& entry) {
    if (entry.view)
        device_.destroyTextureView(entry.view);
    if (entry.texture)
        device_.destroyTexture(entry.texture);
    entry = {};
}

std::optional<rhi::TextureBinding> TextureGpuCache::prepare(TextureHandle handle,
                                                            Texture& texture) {
    if (!handle || !validateTexture(texture.desc(), texture.mipData()))
        return std::nullopt;
    const std::uint64_t cacheKey = key(handle);
    if (const auto found = entries_.find(cacheKey);
        found != entries_.end() && found->second.textureVersion == texture.version()) {
        return rhi::TextureBinding{found->second.view, sampler_};
    }

    if (const auto found = entries_.find(cacheKey); found != entries_.end()) {
        device_.waitIdle();
        destroy(found->second);
        entries_.erase(found);
    }

    const TextureDesc& desc = texture.desc();
    Entry entry;
    entry.textureVersion = texture.version();
    entry.texture = device_.createTexture({
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
    for (const TextureMipData& mip : texture.mipData()) {
        uploads.push_back({mipLevel++, 0, mip.width, mip.height, mip.bytes});
    }
    device_.uploadTexture(entry.texture, uploads);
    entry.view = device_.createTextureView({
        .texture = entry.texture,
        .format = toRhi(desc.format),
        .baseMipLevel = 0,
        .mipCount = desc.mipCount,
    });
    texture.markClean();
    const rhi::TextureBinding result{entry.view, sampler_};
    entries_.emplace(cacheKey, std::move(entry));
    return result;
}

void TextureGpuCache::invalidate(TextureHandle handle) {
    const auto found = entries_.find(key(handle));
    if (found == entries_.end())
        return;
    device_.waitIdle();
    destroy(found->second);
    entries_.erase(found);
}

void TextureGpuCache::clear() {
    for (auto& [unused, entry] : entries_) {
        (void)unused;
        destroy(entry);
    }
    entries_.clear();
}

} // namespace engine
