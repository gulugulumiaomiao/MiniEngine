#include "render/gpu/texture/TextureGpuManager.h"

#include "core/logging/Log.h"
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
    auto replaced =
        cache_.extractIf([source = TextureGpuCache::sourceKey(handle)](
                            const TextureGpuCacheKey& candidate, const TextureGpuResource&) {
            return candidate.source == source;
        });
    if (!replaced.empty())
        device_->waitIdle();
    for (auto& [unused, resource] : replaced) {
        (void)unused;
        textureFactory_->release(resource);
    }
    if (auto duplicate = cache_.put(key, std::move(created)))
        textureFactory_->release(*duplicate);
    texture->markClean();
    const TextureGpuResource* stored = cache_.find(key);
    return stored ? std::optional{rhi::TextureBinding{stored->view, defaultSampler_}}
                  : std::nullopt;
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
    auto removed = cache_.extractIf(
        [source = TextureGpuCache::sourceKey(handle)](const TextureGpuCacheKey& candidate,
                                                      const TextureGpuResource&) {
            return candidate.source == source;
        });
    if (!removed.empty())
        device_->waitIdle();
    for (auto& [unused, resource] : removed) {
        (void)unused;
        textureFactory_->release(resource);
    }
}

void TextureGpuManager::shutdown() {
    if (!initialized())
        return;
    for (auto& [unused, resource] : cache_.extractAll()) {
        (void)unused;
        textureFactory_->release(resource);
    }
    if (samplerFactory_)
        samplerFactory_->release(defaultSampler_);
    samplerFactory_.reset();
    textureFactory_.reset();
    device_ = nullptr;
}

} // namespace engine
