#pragma once

#include "render/pipeline/RenderPass.h"

#include "render/queue/RenderQueue.h"
#include "rhi/api/RhiTypes.h"

namespace engine {

class ForwardPass final : public IRenderPass {
public:
    explicit ForwardPass(DrawFilter filter = DrawFilter{RenderQueueRange::all()});

    void execute(RenderContext& context,
                 RenderGraph& graph,
                 rhi::BindGroupHandle sceneBindGroup,
                 const DrawList& drawList) override;

private:
    DrawFilter filter_;
};

} // namespace engine
