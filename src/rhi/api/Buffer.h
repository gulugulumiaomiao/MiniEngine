#pragma once

#include <cstdint>

namespace engine::rhi {

class IBuffer {
public:
    virtual ~IBuffer() = default;

    [[nodiscard]] virtual std::uint64_t size() const = 0;
};

} // namespace engine::rhi
