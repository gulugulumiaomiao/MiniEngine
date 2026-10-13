#include "render/pipeline/MiniForwardPipeline.h"
#include "render/pipeline/RenderPass.h"
#include "render/pipeline/passes/DepthOnlyPass.h"
#include "render/pipeline/passes/ForwardPass.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/pipeline/RenderContext.h"

#include <cassert>
#include <memory>
#include <string>

namespace {

class CountingPass final : public engine::IRenderPass {
public:
    void buildGraph(engine::RenderContext&, engine::RenderGraph&) override {}
    void updateFrame(engine::RenderContext&, const engine::DrawList&) override {
        ++count;
    }

    int count = 0;
};

} // namespace

int main() {
    using namespace engine;

    MiniForwardPipeline pipeline;
    assert(pipeline.passes().size() == 3);

    // Verify default pass order.
    assert(dynamic_cast<const ShadowCasterPass*>(pipeline.passes()[0].get()) != nullptr);
    assert(dynamic_cast<const DepthOnlyPass*>(pipeline.passes()[1].get()) != nullptr);
    assert(dynamic_cast<const ForwardPass*>(pipeline.passes()[2].get()) != nullptr);

    // Verify passes can be replaced.
    std::vector<std::unique_ptr<IRenderPass>> customPasses;
    customPasses.push_back(std::make_unique<CountingPass>());
    customPasses.push_back(std::make_unique<CountingPass>());
    pipeline.setPasses(std::move(customPasses));
    assert(pipeline.passes().size() == 2);

    // Verify addPass appends.
    pipeline.addPass(std::make_unique<CountingPass>());
    assert(pipeline.passes().size() == 3);

    // A culled scene object does not occupy a row in the compact GPU object table.
    // Filtering must read the surviving item's original scene layer.
    RenderScene scene;
    scene.submit(RenderObject{.layerMask = 2U});
    scene.submit(RenderObject{.layerMask = 1U});
    RgTexturePool pool;
    RgRenderPassCache cache;
    RenderContext context(pool, cache, scene);
    DrawList list;
    DrawItem visible;
    visible.sceneObjectIndex = 1U;
    visible.arguments.firstInstance = 0U;
    list.items.push_back(visible);
    std::vector<DrawItem> filtered;
    filterDrawItems(context, list, RenderPhase::Forward,
                    DrawFilter{RenderQueueRange::all(), 1U}, filtered);
    if (filtered.size() != 1U || filtered[0].arguments.firstInstance != 0U) return 1;
    filterDrawItems(context, list, RenderPhase::DepthOnly,
                    DrawFilter{RenderQueueRange::all(), 1U}, filtered);
    if (!filtered.empty()) return 2;

    return 0;
}
