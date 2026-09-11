#pragma once

#include "render/gpu/common/GpuResourceKey.h"
#include "rhi/api/Device.h"

#include <cstdint>

namespace engine {

// Manager-side helper: drops every cached upload of one asset and releases it. This lives
// above the cache layer because it needs the device to drain in-flight work first -- the
// caches themselves never touch the RHI.
template <typename Cache, typename Factory>
void releaseBySource(Cache& cache, Factory& factory, rhi::IDevice& device, std::uint64_t source) {
    auto removed = cache.extractIf([source](const SourceVersionKey& candidate, const auto&) {
        return candidate.source == source;
    });
    if (removed.empty())
        return;
    // The retired uploads may still be referenced by submitted work.
    device.waitIdle();
    for (auto& entry : removed)
        factory.release(entry.second);
}

} // namespace engine
