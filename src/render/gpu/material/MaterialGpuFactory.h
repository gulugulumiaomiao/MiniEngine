#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/material/MaterialGpuResource.h"

#include <span>

namespace engine {

class Material;

struct MaterialGpuCreateInfo {
    const Material& material;
    std::span<const rhi::TextureBinding> textures;
};

class MaterialGpuFactory final
    : public IGpuResourceFactory<MaterialGpuCreateInfo, MaterialGpuResource> {
public:
    MaterialGpuFactory(rhi::IDevice& device, rhi::BindGroupLayoutHandle layout)
        : IGpuResourceFactory(device), layout_(layout) {}

    [[nodiscard]] bool create(const MaterialGpuCreateInfo& createInfo,
                              MaterialGpuResource& destination) override;
    void release(MaterialGpuResource& resource) override;

private:
    rhi::BindGroupLayoutHandle layout_;
};

} // namespace engine
