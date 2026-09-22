#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/shader/ShaderStorageEntry.h"
#include "render/shader/ShaderCompilePipeline.h"

namespace engine {

class ShaderStorageFactory final
    : public IGpuResourceFactory<CompiledShader, ShaderStorageEntry> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const CompiledShader& description,
                              ShaderStorageEntry& destination) override;
    void release(ShaderStorageEntry& resource) override;
};

} // namespace engine
