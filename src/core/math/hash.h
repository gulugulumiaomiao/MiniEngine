#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace engine {

class VirtualPath;

using Hash64 = std::uint64_t;

inline constexpr Hash64 kFnv1a64OffsetBasis = 14695981039346656037ULL;
inline constexpr Hash64 kFnv1a64Prime = 1099511628211ULL;

[[nodiscard]] Hash64 hashBytes(std::span<const std::byte> bytes, Hash64 seed = kFnv1a64OffsetBasis);
[[nodiscard]] Hash64 hashString(std::string_view value, Hash64 seed = kFnv1a64OffsetBasis);
[[nodiscard]] std::optional<Hash64> hashFile(const VirtualPath& path);
[[nodiscard]] Hash64 mixHash64(Hash64 value);
[[nodiscard]] Hash64 combineHash(Hash64 seed, Hash64 value);
[[nodiscard]] std::string hashToHex(Hash64 value);

template <std::integral Value> void hashAppend(Hash64& hash, Value value) {
    if constexpr (std::same_as<std::remove_cv_t<Value>, bool>) {
        const std::byte byte{static_cast<unsigned char>(value ? 1U : 0U)};
        hash = hashBytes(std::span{&byte, 1}, hash);
    } else {
        using Unsigned = std::make_unsigned_t<Value>;
        static_assert(sizeof(Unsigned) <= sizeof(std::uintmax_t));
        const std::uintmax_t encoded = static_cast<Unsigned>(value);
        std::byte bytes[sizeof(Unsigned)]{};
        for (std::size_t index = 0; index < sizeof(Unsigned); ++index) {
            bytes[index] = std::byte{static_cast<unsigned char>((encoded >> (index * 8U)) & 0xffU)};
        }
        hash = hashBytes(bytes, hash);
    }
}

template <typename Value>
    requires std::is_enum_v<Value>
void hashAppend(Hash64& hash, Value value) {
    hashAppend(hash, static_cast<std::underlying_type_t<Value>>(value));
}

} // namespace engine
