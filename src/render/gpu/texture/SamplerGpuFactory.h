#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "rhi/api/ResourceDesc.h"
#include "rhi/api/Sampler.h"

namespace engine {

class SamplerGpuFactory final : public IGpuResourceFactory<rhi::SamplerDesc, rhi::SamplerHandle> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const rhi::SamplerDesc& description,
                              rhi::SamplerHandle& destination) override;
    void release(rhi::SamplerHandle& resource) override;
};

} // namespace engine
