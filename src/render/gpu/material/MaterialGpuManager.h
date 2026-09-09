#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/material/MaterialBindingCache.h"
#include "render/material/Material.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <memory>

namespace engine {

class MaterialGpuFactory;

namespace rhi {
class IDevice;
}

class MaterialGpuManager final : public Singleton<MaterialGpuManager> {
public:
    ~MaterialGpuManager();

    [[nodiscard]] bool initialize(rhi::IDevice& device,
                                  rhi::BindGroupLayoutHandle materialLayout,
                                  std::uint32_t frameCount);
    [[nodiscard]] rhi::BindGroupHandle resolve(MaterialHandle handle);
    void beginFrame(std::uint32_t frameIndex);
    void shutdown();
    [[nodiscard]] bool initialized() const { return factory_ != nullptr; }

private:
    friend class Singleton<MaterialGpuManager>;
    MaterialGpuManager();

    [[nodiscard]] static std::uint64_t cacheKey(MaterialHandle handle);

    MaterialBindingCache cache_;
    std::unique_ptr<MaterialGpuFactory> factory_;
};

} // namespace engine

#define MATERIAL_GPU_MANAGER (::engine::MaterialGpuManager::instance())
