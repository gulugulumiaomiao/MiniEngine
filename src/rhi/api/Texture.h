#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstdint>

namespace engine::rhi {

// Thin, opaque handle target. The concrete backend texture (e.g. VulkanTexture) retains its own
// native creation info (VkImage / VkFormat / extent / usage ...) and exposes backend-specific
// accessors; the RHI interface carries no descriptors and no view management — views are
// deduped and owned at the device level (IDevice::createTextureView / defaultTextureView).
class IRHITexture {
public:
    virtual ~IRHITexture() = default;
};

} // namespace engine::rhi
