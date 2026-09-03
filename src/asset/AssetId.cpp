#include "asset/AssetId.h"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>

namespace engine {
namespace {

[[nodiscard]] bool parseHex(std::string_view text, std::uint64_t& value) {
    value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        value, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

[[nodiscard]] std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

} // namespace

AssetId AssetId::generate() {
    static std::atomic_uint64_t sequence{
        static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    const std::uint64_t seed =
        sequence.fetch_add(0x9e3779b97f4a7c15ULL, std::memory_order_relaxed);
    std::uint64_t high = mix(seed);
    std::uint64_t low = mix(seed + 0x9e3779b97f4a7c15ULL);

    // RFC 4122 variant and version-four bits make the textual form familiar,
    // while AssetId itself remains an engine-owned opaque identifier.
    high = (high & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;
    low = (low & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;
    if (high == 0 && low == 0) {
        low = 1;
    }
    return {high, low};
}

std::optional<AssetId> AssetId::parse(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-') {
        return std::nullopt;
    }

    std::array<char, 32> compact{};
    std::size_t output = 0;
    for (const char character : text) {
        if (character != '-') {
            compact[output++] = character;
        }
    }

    std::uint64_t high = 0;
    std::uint64_t low = 0;
    if (output != compact.size() ||
        !parseHex({compact.data(), 16}, high) ||
        !parseHex({compact.data() + 16, 16}, low)) {
        return std::nullopt;
    }

    const AssetId result{high, low};
    return result.valid() ? std::optional<AssetId>{result} : std::nullopt;
}

std::string AssetId::toString() const {
    char result[37]{};
    std::snprintf(result, sizeof(result), "%08llx-%04llx-%04llx-%04llx-%012llx",
                  static_cast<unsigned long long>(high_ >> 32U),
                  static_cast<unsigned long long>((high_ >> 16U) & 0xffffULL),
                  static_cast<unsigned long long>(high_ & 0xffffULL),
                  static_cast<unsigned long long>(low_ >> 48U),
                  static_cast<unsigned long long>(low_ & 0xffffffffffffULL));
    return result;
}

} // namespace engine
