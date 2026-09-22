#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace engine {

// A resource identifier packed into a single 64-bit value: the generation occupies the
// high 32 bits and the index the low 32 bits. HandlePool assigns every live slot a
// generation >= 1, so a value of 0 (index 0, generation 0) denotes an invalid RID.
class RID {
public:
    constexpr RID() = default;
    constexpr RID(std::uint32_t index, std::uint32_t generation)
        : value_{(static_cast<std::uint64_t>(generation) << 32U) | index} {}

    [[nodiscard]] constexpr std::uint32_t index() const {
        return static_cast<std::uint32_t>(value_ & 0xFFFFFFFFULL);
    }
    [[nodiscard]] constexpr std::uint32_t generation() const {
        return static_cast<std::uint32_t>(value_ >> 32U);
    }
    [[nodiscard]] constexpr std::uint64_t value() const { return value_; }

    [[nodiscard]] explicit constexpr operator bool() const { return value_ != 0; }
    [[nodiscard]] constexpr bool operator==(const RID&) const = default;

private:
    std::uint64_t value_{};
};

} // namespace engine

namespace std {
template <> struct hash<engine::RID> {
    [[nodiscard]] std::size_t operator()(const engine::RID& rid) const noexcept {
        return std::hash<std::uint64_t>{}(rid.value());
    }
};
} // namespace std
