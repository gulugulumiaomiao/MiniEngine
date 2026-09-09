#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/mesh/MeshGpuResource.h"

namespace engine {

class Mesh;

struct MeshGpuCreateInfo {
    const Mesh& mesh;
};

class MeshGpuFactory final : public IGpuResourceFactory<MeshGpuCreateInfo, MeshGpuResource> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const MeshGpuCreateInfo& createInfo,
                              MeshGpuResource& destination) override;
    void release(MeshGpuResource& resource) override;
};

} // namespace engine
