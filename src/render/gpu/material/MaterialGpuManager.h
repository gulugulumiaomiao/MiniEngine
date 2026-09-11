#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/material/MaterialBindingCache.h"
#include "render/material/Material.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine {

class MaterialGpuFactory;

namespace rhi {
class IDevice;
}

class MaterialGpuManager final : public Singleton<MaterialGpuManager> {
public:
    // Upper bound on simultaneously resident materials. Slots beyond this count
    // are recycled least-recently-used first.
    static constexpr std::uint32_t kMaxResidentMaterials = 512;

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
    // Resolves the material's texture properties into textureScratch_ and reports whether
    // every one of them is available.
    [[nodiscard]] bool collectTextureBindings(const Material& material);

    MaterialBindingCache cache_;
    std::unique_ptr<MaterialGpuFactory> factory_;
    // Reused across resolve() calls so rebuilding a material's texture signature does not
    // allocate every time.
    std::vector<rhi::TextureBinding> textureScratch_;
};

} // namespace engine

#define MATERIAL_GPU_MANAGER (::engine::MaterialGpuManager::instance())
