#pragma once

#include "core/base/Singleton.h"
#include "render/gpu/material/MaterialStorageCache.h"
#include "render/material/Material.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace engine {

class MaterialStorageFactory;
class Mesh;

struct MaterialPassState {
    const ShaderPass* pass{};
    rhi::RID pipeline;

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(pipeline); }
};

namespace rhi {
class IDevice;
}

class MaterialStorage final : public Singleton<MaterialStorage> {
public:
    // Upper bound on simultaneously resident materials. Slots beyond this count
    // are recycled least-recently-used first.
    static constexpr std::uint32_t kMaxResidentMaterials = 512;

    ~MaterialStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device,
                                  rhi::RID materialLayout,
                                  std::uint32_t frameCount);
    [[nodiscard]] rhi::RID resolve(Material& material);
    [[nodiscard]] MaterialPassState resolvePass(const Material& material,
                                                const Mesh& mesh,
                                                ShaderPassType passType,
                                                std::string_view renderPipeline,
                                                rhi::PixelFormat colorFormat,
                                                rhi::PixelFormat depthFormat);
    void invalidate(RID handle);
    void beginFrame(std::uint32_t frameIndex);
    void shutdown();
    [[nodiscard]] bool initialized() const { return factory_ != nullptr; }

private:
    friend class Singleton<MaterialStorage>;
    MaterialStorage();

    [[nodiscard]] static std::uint64_t cacheKey(RID handle);
    // Resolves the material's texture properties into textureScratch_ and reports whether
    // every one of them is available.
    [[nodiscard]] bool collectTextureBindings(const Material& material);

    rhi::IDevice* device_{};
    MaterialStorageCache cache_;
    std::unique_ptr<MaterialStorageFactory> factory_;
    // Reused across resolve() calls so rebuilding a material's texture signature does not
    // allocate every time.
    std::vector<TextureBinding> textureScratch_;
};

} // namespace engine

#define MATERIAL_STORAGE (::engine::MaterialStorage::instance())
