#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/mesh/MeshStorageEntry.h"

namespace engine {

class Mesh;

struct MeshStorageCreateInfo {
    const Mesh& mesh;
};

class MeshStorageFactory final
    : public IGpuResourceFactory<MeshStorageCreateInfo, MeshStorageEntry> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const MeshStorageCreateInfo& createInfo,
                              MeshStorageEntry& destination) override;
    void release(MeshStorageEntry& resource) override;
};

} // namespace engine
