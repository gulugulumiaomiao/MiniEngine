#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstdint>

namespace engine::rhi {

struct TextureViewDesc;

class IRHITexture {
public:
    virtual ~IRHITexture() = default;

    [[nodiscard]] virtual TextureType type() const = 0;
    [[nodiscard]] virtual PixelFormat format() const = 0;
    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    [[nodiscard]] virtual std::uint32_t depth() const = 0;
    [[nodiscard]] virtual std::uint32_t arrayLayers() const = 0;
    [[nodiscard]] virtual std::uint32_t mipCount() const = 0;
    [[nodiscard]] virtual RID defaultView() const = 0;
    [[nodiscard]] virtual RID createView(const TextureViewDesc& desc) = 0;
};

} // namespace engine::rhi
