#pragma once

#include <cstdint>
#include <limits>

namespace engine {

inline constexpr std::uint32_t kInvalidHandleIndex = std::numeric_limits<std::uint32_t>::max();

template <class Tag> struct Handle {
    std::uint32_t index{kInvalidHandleIndex};
    std::uint32_t generation{};

    [[nodiscard]] explicit operator bool() const { return index != kInvalidHandleIndex; }
    bool operator==(const Handle&) const = default;
};

} // namespace engine
