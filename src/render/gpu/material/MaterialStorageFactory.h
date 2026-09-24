#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/material/MaterialStorageEntry.h"

#include <span>

namespace engine {

class Material;

struct MaterialStorageCreateInfo {
    const Material& material;
    std::span<const rhi::TextureBinding> textures;
};

class MaterialStorageFactory final
    : public IGpuResourceFactory<MaterialStorageCreateInfo, MaterialStorageEntry> {
public:
    MaterialStorageFactory(rhi::IDevice& device, rhi::RID layout)
        : IGpuResourceFactory(device), layout_(layout) {}

    [[nodiscard]] bool create(const MaterialStorageCreateInfo& createInfo,
                              MaterialStorageEntry& destination) override;
    void release(MaterialStorageEntry& resource) override;

    // Uniform-only update for materials whose bind group is already valid.
    // Keeps the existing bind group and only refreshes the uniform payload.
    [[nodiscard]] bool updateUniforms(const Material& material,
                                      MaterialStorageEntry& resource);

private:
    rhi::RID layout_;
};

} // namespace engine
