#pragma once

namespace engine::rhi {

enum class SamplerFilter {
    Nearest,
    Linear,
};

class ISampler {
public:
    virtual ~ISampler() = default;

    [[nodiscard]] virtual SamplerFilter filter() const = 0;
};

} // namespace engine::rhi
