#include "render/pipeline/MiniForwardPipeline.h"
#include "render/pipeline/RenderPass.h"
#include "render/pipeline/passes/DepthOnlyPass.h"
#include "render/pipeline/passes/ForwardPass.h"
#include "render/pipeline/passes/ShadowCasterPass.h"

#include <cassert>
#include <memory>
#include <string>

namespace {

class CountingPass final : public engine::IRenderPass {
public:
    void execute(engine::RenderContext&, engine::RenderGraph&, const engine::DrawList&) override {
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

    return 0;
}
