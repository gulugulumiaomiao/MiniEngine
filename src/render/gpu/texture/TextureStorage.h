#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/texture/TextureStorageCache.h"

#include <memory>

namespace engine {

class TextureStorageFactory;
class Texture;

namespace rhi {
class IDevice;
struct TextureViewDesc;
}

class TextureStorage final : public Singleton<TextureStorage> {
public:
    ~TextureStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] const TextureStorageEntry* resolve(Texture& texture);
    [[nodiscard]] TextureBinding resolveBinding(Texture& texture);
    [[nodiscard]] TextureView getView(Texture& texture, rhi::TextureViewDesc desc);
    void invalidate(RID handle);
    void shutdown();
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    friend class Singleton<TextureStorage>;
    TextureStorage();

    rhi::IDevice* device_{};
    TextureStorageCache cache_;
    std::unique_ptr<TextureStorageFactory> factory_;
};

} // namespace engine

#define TEXTURE_STORAGE (::engine::TextureStorage::instance())
