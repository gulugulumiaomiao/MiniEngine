#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace engine {

class AssetId final {
public:
    constexpr AssetId() = default;
    constexpr AssetId(std::uint64_t high, std::uint64_t low)
        : high_(high), low_(low) {}

    [[nodiscard]] static AssetId generate();
    [[nodiscard]] static std::optional<AssetId> parse(std::string_view text);

    [[nodiscard]] constexpr bool valid() const {
        return high_ != 0 || low_ != 0;
    }
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] constexpr std::uint64_t high() const { return high_; }
    [[nodiscard]] constexpr std::uint64_t low() const { return low_; }

    bool operator==(const AssetId&) const = default;

private:
    std::uint64_t high_{};
    std::uint64_t low_{};
};

} // namespace engine

template <>
struct std::hash<engine::AssetId> {
    [[nodiscard]] std::size_t operator()(const engine::AssetId& id) const noexcept {
        const std::size_t high = std::hash<std::uint64_t>{}(id.high());
        const std::size_t low = std::hash<std::uint64_t>{}(id.low());
        return high ^ (low + 0x9e3779b97f4a7c15ULL + (high << 6U) +
                       (high >> 2U));
    }
};
