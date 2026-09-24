#pragma once

#include "rhi/api/ResourceDesc.h"
#include "rhi/api/Texture.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace engine::rhi {

struct TextureViewDesc {
    TextureType type{TextureType::Texture2D};
    // Undefined means use the source texture's pixel format.
    PixelFormat format{PixelFormat::Undefined};
    std::uint32_t baseMip{};
    std::uint32_t mipCount{1};
    std::uint32_t baseLayer{};
    std::uint32_t layerCount{1};
    TextureAspect aspect{TextureAspect::Color};
    TextureSwizzle swizzle;

    [[nodiscard]] bool operator==(const TextureViewDesc&) const = default;
};

struct TextureViewDescHash {
    [[nodiscard]] std::size_t operator()(const TextureViewDesc& desc) const noexcept {
        std::size_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::size_t value) { hash = (hash ^ value) * 1099511628211ULL; };
        mix(static_cast<std::size_t>(desc.type));
        mix(static_cast<std::size_t>(desc.format));
        mix(desc.baseMip);
        mix(desc.mipCount);
        mix(desc.baseLayer);
        mix(desc.layerCount);
        mix(static_cast<std::size_t>(desc.aspect));
        mix(static_cast<std::size_t>(desc.swizzle.r));
        mix(static_cast<std::size_t>(desc.swizzle.g));
        mix(static_cast<std::size_t>(desc.swizzle.b));
        mix(static_cast<std::size_t>(desc.swizzle.a));
        return hash;
    }
};

class IRHITextureView {
public:
    virtual ~IRHITextureView() = default;

    // Back-reference to the owning texture (non-owning). The view no longer retains its
    // TextureViewDesc; the concrete backend view keeps only its native VkImageViewCreateInfo info.
    [[nodiscard]] IRHITexture* texture() const { return texture_; }

protected:
    explicit IRHITextureView(IRHITexture* texture) : texture_(texture) {}

private:
    IRHITexture* texture_{};
};

} // namespace engine::rhi
