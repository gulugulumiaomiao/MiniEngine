#pragma once

#include "core/base/RID.h"

#include <cstdint>

namespace engine {

// Packs a render handle into a single integer so it can be used as a cache key. RID already
// stores the generation in the high half, which keeps recycled indices distinct.
[[nodiscard]] inline std::uint64_t handleKey(RID handle) { return handle.value(); }

} // namespace engine
