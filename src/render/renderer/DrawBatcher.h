#pragma once

#include "render/renderer/DrawList.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine {

struct BatchedRenderItems {
    RenderItemList items;
    std::vector<std::uint32_t> instanceRows;
    std::size_t itemCount{};
    std::size_t gpuInstancedBatchCount{};
};

struct DrawBatcherLimits {
    std::uint32_t maxGpuInstancesPerDraw{1024};
};

class DrawBatcher final {
public:
    explicit DrawBatcher(DrawBatcherLimits limits = {}) : limits_(limits) {}
    [[nodiscard]] BatchedRenderItems build(std::span<const DrawItem> items);

private:
    [[nodiscard]] static bool sameBatchState(const DrawItem& lhs, const DrawItem& rhs);
    DrawBatcherLimits limits_;
};

} // namespace engine
