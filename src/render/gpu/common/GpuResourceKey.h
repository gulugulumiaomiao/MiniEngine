#pragma once

#include "core/hash.h"

#include <cstddef>
#include <cstdint>

namespace engine {

// Cache key for GPU resources uploaded from a versioned asset: the source identifies the
// asset handle and the version retires uploads whose CPU-side data has changed.
struct SourceVersionKey {
    std::uint64_t source{};
    std::uint64_t version{};

    bool operator==(const SourceVersionKey&) const = default;
};

struct SourceVersionKeyHash {
    [[nodiscard]] std::size_t operator()(const SourceVersionKey& key) const {
        Hash64 hash = kFnv1a64OffsetBasis;
        hashAppend(hash, key.source);
        hashAppend(hash, key.version);
        return static_cast<std::size_t>(hash);
    }
};

// Packs a render handle into a single integer so it can be used as a cache key. The
// generation occupies the high half, which keeps recycled indices distinct.
template <typename Handle> [[nodiscard]] std::uint64_t handleKey(Handle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

} // namespace engine
