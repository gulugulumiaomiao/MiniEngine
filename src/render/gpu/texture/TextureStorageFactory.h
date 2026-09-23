#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/texture/TextureStorageEntry.h"

namespace engine {

class Texture;

struct TextureStorageCreateInfo {
    // Non-const: create() moves the transient CPU pixel blob out for the one-shot upload.
    Texture& texture;
};

class TextureStorageFactory final
    : public IGpuResourceFactory<TextureStorageCreateInfo, TextureStorageEntry> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const TextureStorageCreateInfo& createInfo,
                              TextureStorageEntry& destination) override;
    void release(TextureStorageEntry& resource) override;
};

} // namespace engine
