#pragma once

#include "rhi/api/PipelineDesc.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace engine::rhi {

enum class SamplerFilter {
    Nearest,
    Linear,
};

enum class SamplerMipmapFilter { Nearest, Linear };
enum class SamplerAddressMode { Repeat, MirroredRepeat, ClampToEdge };
enum class SamplerBorderColor { TransparentBlack, OpaqueBlack, OpaqueWhite };

// Sentinel matching VK_LOD_CLAMP_NONE.
inline constexpr float kLodClampNone = 1000.0F;

struct SamplerDesc {
    SamplerFilter minFilter{SamplerFilter::Linear};
    SamplerFilter magFilter{SamplerFilter::Linear};
    SamplerMipmapFilter mipmapFilter{SamplerMipmapFilter::Linear};
    SamplerAddressMode addressU{SamplerAddressMode::Repeat};
    SamplerAddressMode addressV{SamplerAddressMode::Repeat};
    SamplerBorderColor borderColor{SamplerBorderColor::OpaqueWhite};
    float maxAnisotropy{1.0F};
    float minLod{0.0F};
    float maxLod{kLodClampNone};
    bool compareEnable{false};
    CompareOp compareOp{CompareOp::LessEqual};

    [[nodiscard]] bool operator==(const SamplerDesc&) const = default;
};

// Descriptor-value hash so a SamplerDesc can key a device-side dedup cache: samplers are pure
// value objects, so identical descriptors resolve to one shared handle.
struct SamplerDescHash {
    [[nodiscard]] std::size_t operator()(const SamplerDesc& desc) const noexcept {
        std::size_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::size_t value) { hash = (hash ^ value) * 1099511628211ULL; };
        mix(static_cast<std::size_t>(desc.minFilter));
        mix(static_cast<std::size_t>(desc.magFilter));
        mix(static_cast<std::size_t>(desc.mipmapFilter));
        mix(static_cast<std::size_t>(desc.addressU));
        mix(static_cast<std::size_t>(desc.addressV));
        mix(static_cast<std::size_t>(desc.borderColor));
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.maxAnisotropy)));
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.minLod)));
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.maxLod)));
        mix(static_cast<std::size_t>(desc.compareEnable));
        mix(static_cast<std::size_t>(desc.compareOp));
        return hash;
    }
};

class IRHISampler {
public:
    virtual ~IRHISampler() = default;

    [[nodiscard]] virtual const SamplerDesc& desc() const = 0;
};

} // namespace engine::rhi
