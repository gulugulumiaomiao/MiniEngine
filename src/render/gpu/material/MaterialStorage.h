#pragma once

#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/gpu/material/MaterialStorageCache.h"
#include "render/material/Material.h"
#include "render/uniform/UniformSet.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace engine {

class Mesh;
class ShaderProgram;

// The per-draw resolution result: the selected pass, its graphics pipeline and the material's
// set-1 UniformSet. Valid when the pipeline was produced; materialSet may be empty for passes
// whose SPIR-V stripped the material block (e.g. depth-only shadow casters), which legitimately
// have no set 1 to bind.
struct MaterialPassState {
    const ShaderPass* pass{};
    rhi::RID pipeline;
    Ref<UniformSet> materialSet;
    // Number of descriptor sets the resolved program's pipeline layout declares. The renderer binds
    // set i only when i < setCount, so a pass whose SPIR-V stripped set 1/set 2 (e.g. a depth-only
    // shadow caster) never gets a descriptor set bound beyond its layout.
    std::uint32_t setCount{};

    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(pipeline); }
};

namespace rhi {
class IDevice;
}

// Layer-2 store of per-material set-1 UniformSets, resident across frames in an LRU cache and
// refreshed from Material::version() and the resolved texture signature. The set-1 layout comes
// from the program's reflection, so a material whose shader changes rebuilds its set.
class MaterialStorage final : public Singleton<MaterialStorage> {
public:
    // Upper bound on simultaneously resident materials. Slots beyond this count
    // are recycled least-recently-used first.
    static constexpr std::uint32_t kMaxResidentMaterials = 512;

    ~MaterialStorage();

    [[nodiscard]] bool initialize(rhi::IDevice& device, std::uint32_t frameCount);
    // Builds/refreshes the material's set-1 UniformSet from the program's reflected set-1 layout.
    [[nodiscard]] Ref<UniformSet> resolve(const Material& material, const ShaderProgram& program);
    [[nodiscard]] MaterialPassState resolvePass(const Material& material,
                                                const Mesh& mesh,
                                                ShaderPassType passType,
                                                std::string_view renderPipeline,
                                                rhi::PixelFormat colorFormat,
                                                rhi::PixelFormat depthFormat);
    void invalidate(RID handle);
    void beginFrame(std::uint32_t frameIndex);
    void shutdown();
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    friend class Singleton<MaterialStorage>;
    MaterialStorage();

    [[nodiscard]] static std::uint64_t cacheKey(RID handle);
    // Resolves the material's texture properties into textureScratch_ and reports whether every
    // one of them is available.
    [[nodiscard]] bool collectTextureBindings(const Material& material);

    rhi::IDevice* device_{};
    MaterialStorageCache cache_;
    // Reused across resolve() calls so rebuilding a material's texture signature does not
    // allocate every time.
    std::vector<rhi::TextureBinding> textureScratch_;
};

} // namespace engine

#define MATERIAL_STORAGE (::engine::MaterialStorage::instance())
