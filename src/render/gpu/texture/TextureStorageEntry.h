#pragma once

#include "render/texture/TextureView.h"

namespace engine {

struct TextureStorageEntry {
    rhi::RID texture;
    TextureView defaultView;
    Sampler defaultSampler;
};

} // namespace engine
