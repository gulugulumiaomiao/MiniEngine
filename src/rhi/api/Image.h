#pragma once

#include "rhi/api/PipelineDesc.h"

#include <cstdint>

namespace engine::rhi {

class IImage {
public:
    virtual ~IImage() = default;

    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    [[nodiscard]] virtual std::uint32_t depth() const = 0;
    [[nodiscard]] virtual std::uint32_t mipCount() const = 0;
    [[nodiscard]] virtual TextureFormat format() const = 0;
};

} // namespace engine::rhi
