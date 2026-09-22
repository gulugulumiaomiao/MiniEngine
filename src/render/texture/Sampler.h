#pragma once

#include "rhi/api/RhiTypes.h"
#include "rhi/api/Sampler.h"

namespace engine {

namespace rhi {
class IDevice;
}

// Lightweight, non-owning reference to a device-cached sampler.
class Sampler final {
public:
    Sampler() = default;

    [[nodiscard]] static Sampler resolve(rhi::IDevice& device, const rhi::SamplerDesc& desc);

    [[nodiscard]] rhi::RID rhiHandle() const { return handle_; }
    [[nodiscard]] const rhi::SamplerDesc& desc() const { return desc_; }
    [[nodiscard]] explicit operator bool() const { return static_cast<bool>(handle_); }
    [[nodiscard]] bool operator==(const Sampler&) const = default;

private:
    Sampler(rhi::RID handle, rhi::SamplerDesc desc) : handle_(handle), desc_(desc) {}

    rhi::RID handle_;
    rhi::SamplerDesc desc_;
};

} // namespace engine
