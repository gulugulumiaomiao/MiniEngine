#include "render/gpu/GpuCacheRegistry.h"

#include "core/logging/Log.h"

namespace engine {

bool GpuCacheRegistry::initialize(std::uint32_t frameCount) {
    if (initialized_ || meshes_.size() != 0 || textures_.size() != 0 || shaders_.size() != 0 ||
        pipelines_.size() != 0 || materials_.size() != 0 || !materials_.initialize(frameCount)) {
        Log::error("GpuCacheRegistry", "Cannot initialize a non-empty GPU cache");
        return false;
    }
    initialized_ = true;
    return true;
}

bool GpuCacheRegistry::shutdown() {
    if (!initialized_)
        return true;
    if (meshes_.size() != 0 || textures_.size() != 0 || shaders_.size() != 0 ||
        pipelines_.size() != 0 || materials_.size() != 0) {
        Log::error("GpuCacheRegistry", "GPU resources must be extracted before shutdown");
        return false;
    }
    materials_.reset();
    initialized_ = false;
    return true;
}

} // namespace engine
