#pragma once

#include <cstdint>
#include <limits>

namespace engine {

inline constexpr std::uint32_t kInvalidResourceIndex =
    std::numeric_limits<std::uint32_t>::max();

struct MeshHandle {
    std::uint32_t index{kInvalidResourceIndex};
    std::uint32_t generation{};

    [[nodiscard]] explicit operator bool() const { return index != kInvalidResourceIndex; }
};

struct MaterialHandle {
    std::uint32_t index{kInvalidResourceIndex};
    std::uint32_t generation{};

    [[nodiscard]] explicit operator bool() const { return index != kInvalidResourceIndex; }
};

} // namespace engine
