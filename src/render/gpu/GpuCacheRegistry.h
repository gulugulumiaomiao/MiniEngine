#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/material/MaterialBindingCache.h"
#include "render/gpu/mesh/MeshGpuCache.h"
#include "render/gpu/pipeline/GraphicsPipelineCache.h"
#include "render/gpu/shader/ShaderModuleCache.h"
#include "render/gpu/texture/TextureGpuCache.h"

#include <cstdint>

namespace engine {

class GpuCacheRegistry final : public Singleton<GpuCacheRegistry> {
public:
    [[nodiscard]] bool initialize(std::uint32_t frameCount);
    [[nodiscard]] bool shutdown();
    [[nodiscard]] bool initialized() const { return initialized_; }

    [[nodiscard]] MeshGpuCache& meshes() { return meshes_; }
    [[nodiscard]] TextureGpuCache& textures() { return textures_; }
    [[nodiscard]] MaterialBindingCache& materials() { return materials_; }
    [[nodiscard]] ShaderModuleCache& shaders() { return shaders_; }
    [[nodiscard]] GraphicsPipelineCache& pipelines() { return pipelines_; }

private:
    friend class Singleton<GpuCacheRegistry>;
    GpuCacheRegistry() = default;

    MeshGpuCache meshes_;
    TextureGpuCache textures_;
    MaterialBindingCache materials_;
    ShaderModuleCache shaders_;
    GraphicsPipelineCache pipelines_;
    bool initialized_{};
};

} // namespace engine

#define GPU_CACHE (::engine::GpuCacheRegistry::instance())
