#include "render/gpu/texture/TextureStorage.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuManagerUtils.h"
#include "render/gpu/texture/TextureStorageFactory.h"
#include "render/texture/TextureManager.h"
#include "rhi/api/Device.h"
#include "rhi/api/Texture.h"

#include <utility>

namespace engine {

TextureStorage::TextureStorage() = default;
TextureStorage::~TextureStorage() = default;

bool TextureStorage::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("TextureStorage", "Storage is already initialized");
        return false;
    }
    device_ = &device;
    factory_ = std::make_unique<TextureStorageFactory>(device);
    TEXTURE_RESOURCE_MANAGER.setDestroyObserver([this](RID handle) { invalidate(handle); });
    return true;
}

const TextureStorageEntry* TextureStorage::resolve(const Texture& texture) {
    if (!initialized() || !texture.resourceId())
        return nullptr;

    const RID handle = texture.resourceId();
    const TextureStorageCacheKey key = TextureStorageCache::key(handle, texture.version());
    if (const TextureStorageEntry* cached = cache_.find(key))
        return cached;

    TextureStorageEntry created;
    if (!factory_->create({texture}, created))
        return nullptr;
    releaseBySource(cache_, *factory_, *device_, TextureStorageCache::sourceKey(handle));
    auto stored = cache_.store(key, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    return stored.stored;
}

TextureBinding TextureStorage::resolveBinding(const Texture& texture) {
    const TextureStorageEntry* resource = resolve(texture);
    return resource ? TextureBinding{resource->defaultView, resource->defaultSampler}
                    : TextureBinding{};
}

TextureView TextureStorage::getView(const Texture& texture, rhi::TextureViewDesc desc) {
    const TextureStorageEntry* resource = resolve(texture);
    if (!resource || !device_)
        return {};
    if (desc.format == rhi::PixelFormat::Undefined)
        desc.format = resource->defaultView.desc().format;
    rhi::IRHITexture* rhiTexture = device_->resolveTextureResource(resource->texture);
    if (!rhiTexture)
        return {};
    const rhi::RID view = rhiTexture->createView(desc);
    return view ? TextureView{resource->texture, view, desc} : TextureView{};
}

void TextureStorage::invalidate(RID handle) {
    if (!initialized())
        return;
    releaseBySource(cache_, *factory_, *device_, TextureStorageCache::sourceKey(handle));
}

void TextureStorage::shutdown() {
    TEXTURE_RESOURCE_MANAGER.setDestroyObserver({});
    if (!initialized())
        return;
    auto resources = cache_.extractAll();
    if (!resources.empty())
        device_->waitIdle();
    for (auto& entry : resources)
        factory_->release(entry.second);
    factory_.reset();
    device_ = nullptr;
}

} // namespace engine
