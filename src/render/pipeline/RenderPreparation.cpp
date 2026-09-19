#include "render/pipeline/RenderPreparation.h"

#include "core/logging/Log.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/material/MaterialGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/renderer/DrawListBuilder.h"
#include "render/renderer/RenderFrameStats.h"
#include "render/renderer/StaticBatcher.h"
#include "render/scene/RenderScene.h"

#include <algorithm>

namespace engine {

std::size_t drawItemCount(const DrawList& drawList) {
    std::size_t count{};
    for (const auto& [queue, items] : drawList.groups) {
        (void)queue;
        count += items.size();
    }
    return count;
}

DrawList preparePipelineDrawList(RenderContext& context, StaticBatcher& staticBatcher) {
    DrawListBuilder builder;
    const SourceDrawData source = builder.extract(context.scene(), context);
    DrawList drawList = builder.prepare(source, context);

    RenderFrameStats& frameStats = context.frameStats();
    frameStats.sourceDrawItems = source.items.size();
    frameStats.preparedDrawItems = drawItemCount(drawList);

    MATERIAL_GPU_MANAGER.beginFrame(context.frameIndex());
    for (auto& [queue, items] : drawList.groups) {
        (void)queue;
        for (DrawItem& item : items) {
            item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.material);
            if (!item.materialBindGroup && item.fallbackPipeline && item.fallbackMaterial) {
                Log::error("RenderPreparation",
                           "Using Error Material after material GPU preparation failed");
                item.pipeline = item.fallbackPipeline;
                item.material = item.fallbackMaterial;
                item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.fallbackMaterial);
            }
        }
        std::erase_if(items,
                      [](const DrawItem& item) { return !item.pipeline || !item.materialBindGroup; });
    }
    std::erase_if(drawList.groups, [](const auto& entry) { return entry.second.empty(); });

    staticBatcher.process(drawList, context.device());
    const StaticBatcherStats& staticStats = staticBatcher.stats();
    frameStats.staticSourceItems = staticStats.sourceItems;
    frameStats.staticCombinedDraws = staticStats.combinedDraws;
    frameStats.staticCacheHits = staticStats.cacheHits;
    frameStats.staticCacheMisses = staticStats.cacheMisses;

    FRAME_GPU_MANAGER.beginFrame(context.frameIndex());
    FRAME_GPU_MANAGER.upload(context.frameIndex(), drawList);
    return drawList;
}

} // namespace engine
