#pragma once

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
};

class ISampler {
public:
    virtual ~ISampler() = default;

    [[nodiscard]] virtual SamplerFilter filter() const = 0;
};

} // namespace engine::rhi
