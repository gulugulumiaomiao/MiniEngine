#pragma once

#include <cstdint>
#include <string>

namespace engine::rhi {

enum class BufferUsage : std::uint32_t {
    None = 0,
    Vertex = 1U << 0U,
    Index = 1U << 1U,
    Uniform = 1U << 2U,
    Storage = 1U << 3U,
    TransferSource = 1U << 4U,
    TransferDestination = 1U << 5U,
};

constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) |
                                    static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(BufferUsage value, BufferUsage flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class MemoryUsage {
    DeviceLocal,
    Upload,
    Readback,
};

struct BufferDesc {
    std::uint64_t size{};
    BufferUsage usage{BufferUsage::None};
    MemoryUsage memoryUsage{MemoryUsage::DeviceLocal};
    std::string debugName;
};

class IBuffer {
public:
    virtual ~IBuffer() = default;

    [[nodiscard]] virtual std::uint64_t size() const = 0;
};

} // namespace engine::rhi
