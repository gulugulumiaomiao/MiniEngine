#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/shader/ShaderModuleGpuResource.h"
#include "render/shader/ShaderCompilePipeline.h"

namespace engine {

class ShaderModuleGpuFactory final
    : public IGpuResourceFactory<CompiledShader, ShaderModuleGpuResource> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const CompiledShader& description,
                              ShaderModuleGpuResource& destination) override;
    void release(ShaderModuleGpuResource& resource) override;
};

} // namespace engine
