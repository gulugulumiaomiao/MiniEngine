#include "render/pipeline/RenderPipeline.h"

#include <memory>
#include <string>
#include <utility>

namespace {

class StubPipeline final : public engine::IRenderPipeline {
public:
    void render(engine::RenderContext&) override { rendered_ = true; }

    [[nodiscard]] bool rendered() const { return rendered_; }

private:
    bool rendered_{};
};

} // namespace

int main() {
    engine::RenderPipelineRegistry registry;

    bool factoryCalled = false;
    registry.registerPipeline("Stub", [&factoryCalled]() {
        factoryCalled = true;
        return std::make_unique<StubPipeline>();
    });

    std::unique_ptr<engine::IRenderPipeline> pipeline = registry.create("Stub");
    if (!factoryCalled || pipeline == nullptr) {
        return 1;
    }

    auto* stub = dynamic_cast<StubPipeline*>(pipeline.get());
    if (stub == nullptr || stub->rendered()) {
        return 1;
    }

    return 0;
}
