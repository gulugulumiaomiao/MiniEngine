#pragma once

#include <cstddef>

namespace engine {

struct DrawSubmissionStats {
    std::size_t sourceItems{};
    std::size_t renderItems{};
    std::size_t gpuInstancedDraws{};
};

// Snapshot of CPU preparation and GPU submission work for the last rendered frame.
// Pass callbacks add their submission counts while the render graph executes.
struct RenderFrameStats {
    std::size_t sourceDrawItems{};
    std::size_t preparedDrawItems{};
    std::size_t submittedDrawItems{};
    std::size_t renderItems{};
    std::size_t drawCalls{};
    std::size_t staticSourceItems{};
    std::size_t staticCombinedDraws{};
    std::size_t staticCacheHits{};
    std::size_t staticCacheMisses{};
    std::size_t gpuInstancedDraws{};
    std::size_t renderGraphPasses{};
    std::size_t transientRenderTargets{};
    bool renderGraphPlanCacheHit{};

    void recordSubmission(const DrawSubmissionStats& submission) {
        submittedDrawItems += submission.sourceItems;
        renderItems += submission.renderItems;
        drawCalls += submission.renderItems;
        gpuInstancedDraws += submission.gpuInstancedDraws;
    }
};

} // namespace engine
