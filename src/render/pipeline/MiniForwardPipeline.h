#pragma once

#include "render/pipeline/RenderPipeline.h"

#include "render/renderer/DrawList.h"
#include "rhi/api/RhiTypes.h"

#include <cstdint>

namespace engine {

class RenderContext;

class MiniForwardPipeline final : public IRenderPipeline {
public:
    MiniForwardPipeline() = default;

    void render(RenderContext& context) override;
    void onSwapchainChanged() override;

private:
    void submitDrawList(RenderContext& context, DrawList drawList);
    void recordDrawCommands(RenderContext& context,
                            rhi::BindGroupHandle sceneBindGroup,
                            const DrawList& drawList);
};

} // namespace engine
