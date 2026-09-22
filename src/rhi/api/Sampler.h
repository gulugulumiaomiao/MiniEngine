#pragma once

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

struct SamplerDesc {
    SamplerFilter minFilter{SamplerFilter::Linear};
    SamplerFilter magFilter{SamplerFilter::Linear};
    SamplerMipmapFilter mipmapFilter{SamplerMipmapFilter::Linear};
    SamplerAddressMode addressU{SamplerAddressMode::Repeat};
    SamplerAddressMode addressV{SamplerAddressMode::Repeat};
    float maxAnisotropy{1.0F};

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
        mix(static_cast<std::size_t>(std::bit_cast<std::uint32_t>(desc.maxAnisotropy)));
        return hash;
    }
};

class IRHISampler {
public:
    virtual ~IRHISampler() = default;

    [[nodiscard]] virtual const SamplerDesc& desc() const = 0;
};

} // namespace engine::rhi
