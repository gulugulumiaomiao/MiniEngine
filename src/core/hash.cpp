#include "core/hash.h"

#include "core/filesystem/FileSystem.h"

#include <iomanip>
#include <sstream>

namespace engine {

Hash64 hashBytes(std::span<const std::byte> bytes, Hash64 seed) {
    Hash64 hash = seed;
    for (const std::byte byte : bytes) {
        hash ^= static_cast<Hash64>(std::to_integer<unsigned char>(byte));
        hash *= kFnv1a64Prime;
    }
    return hash;
}

Hash64 hashString(std::string_view value, Hash64 seed) {
    return hashBytes({reinterpret_cast<const std::byte*>(value.data()), value.size()}, seed);
}

std::optional<Hash64> hashFile(const VirtualPath& path) {
    const auto bytes = FILE_SYSTEM.readBinary(path);
    return bytes ? std::optional<Hash64>{hashBytes(*bytes)} : std::nullopt;
}

Hash64 mixHash64(Hash64 value) {
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

Hash64 combineHash(Hash64 seed, Hash64 value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

std::string hashToHex(Hash64 value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

} // namespace engine
