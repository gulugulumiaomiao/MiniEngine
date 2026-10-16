#pragma once
#include "render/texture/Texture.h"

#include <string>
#include <utility>

namespace engine::test {
// Short fixture construction without changing TextureDesc's asset constructors.
inline TextureDesc textureDesc(TextureFormat format, std::uint32_t width, std::uint32_t height,
                               std::string name = {},
                               std::uint8_t samples = 1) {
    TextureDesc desc{TextureType::Texture2D, format, width, height};
    desc.samples = samples;
    desc.debugName = std::move(name);
    return desc;
}
} // namespace engine::test
