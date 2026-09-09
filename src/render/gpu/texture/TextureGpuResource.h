#pragma once

#include "rhi/api/ResourceDesc.h"

namespace engine {

struct TextureGpuResource {
    rhi::TextureHandle texture;
    rhi::TextureViewHandle view;
};

} // namespace engine
