#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/texture/TextureGpuResource.h"

namespace engine {

class Texture;

struct TextureGpuCreateInfo {
    const Texture& texture;
};

class TextureGpuFactory final
    : public IGpuResourceFactory<TextureGpuCreateInfo, TextureGpuResource> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const TextureGpuCreateInfo& createInfo,
                              TextureGpuResource& destination) override;
    void release(TextureGpuResource& resource) override;
};

} // namespace engine
