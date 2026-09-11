#include "render/gpu/texture/TextureGpuManager.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuManagerUtils.h"
#include "render/gpu/texture/SamplerGpuFactory.h"
#include "render/gpu/texture/TextureGpuFactory.h"
#include "render/texture/TextureManager.h"
#include "rhi/api/Device.h"

#include <string>
#include <utility>

namespace engine {

TextureGpuManager::TextureGpuManager() = default;
TextureGpuManager::~TextureGpuManager() = default;

bool TextureGpuManager::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("TextureGpuManager", "Manager is already initialized");
        return false;
    }
    device_ = &device;
    textureFactory_ = std::make_unique<TextureGpuFactory>(device);
    samplerFactory_ = std::make_unique<SamplerGpuFactory>(device);
    if (!samplerFactory_->create({.minFilter = rhi::SamplerFilter::Linear,
                                  .magFilter = rhi::SamplerFilter::Linear,
                                  .mipmapFilter = rhi::SamplerMipmapFilter::Linear,
                                  .addressU = rhi::SamplerAddressMode::Repeat,
                                  .addressV = rhi::SamplerAddressMode::Repeat,
                                  .maxAnisotropy = 1.0F},
                                 defaultSampler_)) {
        shutdown();
        return false;
    }
    return true;
}

std::optional<rhi::TextureBinding> TextureGpuManager::resolve(TextureHandle handle) {
    Texture* texture = TEXTURE_MANAGER.find(handle);
    if (!initialized() || !texture)
        return std::nullopt;

    const TextureGpuCacheKey key = TextureGpuCache::key(handle, texture->version());
    if (const TextureGpuResource* cached = cache_.find(key))
        return rhi::TextureBinding{cached->view, defaultSampler_};

    TextureGpuResource created;
    if (!textureFactory_->create({*texture}, created))
        return std::nullopt;
    // Drop any upload of an earlier version of this Texture before the new one goes resident.
    releaseBySource(cache_, *textureFactory_, *device_, TextureGpuCache::sourceKey(handle));
    auto stored = cache_.store(key, std::move(created));
    if (stored.replaced)
        textureFactory_->release(*stored.replaced);
    texture->markClean();
    return rhi::TextureBinding{stored.stored->view, defaultSampler_};
}

std::optional<rhi::TextureBinding> TextureGpuManager::resolveReference(std::string_view reference) {
    TextureHandle handle;
    if (reference.empty()) {
        handle = TEXTURE_MANAGER.defaultWhite();
    } else {
        VirtualPath path{reference};
        if (!path.valid())
            path = VirtualPath{"asset://" + std::string{reference}};
        if (path.valid() && path.scheme() == "asset")
            handle = TEXTURE_MANAGER.load(path);
        if (!handle) {
            Log::warn("TextureGpuManager",
                      "Using the error Texture for unresolved reference: %.*s",
                      static_cast<int>(reference.size()),
                      reference.data());
            handle = TEXTURE_MANAGER.errorTexture();
        }
    }
    return resolve(handle);
}

void TextureGpuManager::invalidate(TextureHandle handle) {
    if (!initialized())
        return;
    releaseBySource(cache_, *textureFactory_, *device_, TextureGpuCache::sourceKey(handle));
}

void TextureGpuManager::shutdown() {
    if (!initialized())
        return;
    for (auto& entry : cache_.extractAll())
        textureFactory_->release(entry.second);
    if (samplerFactory_)
        samplerFactory_->release(defaultSampler_);
    samplerFactory_.reset();
    textureFactory_.reset();
    device_ = nullptr;
}

} // namespace engine
