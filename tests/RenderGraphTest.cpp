#include "renderer/RenderGraph.h"

#include <string>
#include <vector>

namespace {

class MockGraphicsEncoder final : public engine::rhi::IGraphicsCommandEncoder {
public:
    void resourceBarriers(std::span<const engine::rhi::TextureBarrier> barriers) override {
        events.push_back("barriers:" + std::to_string(barriers.size()));
        recordedBarriers.insert(recordedBarriers.end(), barriers.begin(), barriers.end());
    }
    void beginRendering(const engine::rhi::RenderingInfo&) override {
        events.emplace_back("beginRendering");
    }
    void endRendering() override { events.emplace_back("endRendering"); }
    void setViewport(const engine::rhi::Viewport&) override {}
    void setScissor(const engine::rhi::Rect&) override {}
    void bindPipeline(engine::rhi::GraphicsPipelineHandle) override {}
    void bindVertexBuffer(std::uint32_t, engine::rhi::BufferHandle, std::uint64_t) override {}
    void bindIndexBuffer(engine::rhi::BufferHandle, std::uint64_t,
                         engine::rhi::IndexFormat) override {}
    void bindGroup(std::uint32_t, engine::rhi::BindGroupHandle,
                   std::span<const std::uint32_t>) override {}
    void draw(const engine::rhi::DrawArguments&) override {}
    void drawIndexed(const engine::rhi::DrawIndexedArguments&) override {}
    void beginDebugLabel(std::string_view name, const engine::math::Vec4&) override {
        events.push_back("label:" + std::string{name});
    }
    void endDebugLabel() override { events.emplace_back("endLabel"); }

    std::vector<std::string> events;
    std::vector<engine::rhi::TextureBarrier> recordedBarriers;
};

} // namespace

int main() {
    using namespace engine;
    const rhi::TextureHandle texture{3, 7};
    const rhi::TextureViewHandle view{3, 7};

    RenderGraph graph;
    graph.importTexture({texture, rhi::ResourceState::Undefined,
                         rhi::ResourceState::Present, rhi::TextureAspect::Color});
    rhi::RenderingInfo rendering;
    rendering.renderArea = {0, 0, 1280, 720};
    rendering.colorAttachments.push_back(
        {view, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.0F, 0.0F, 0.0F, 1.0F}});
    graph.addGraphicsPass(
        "Forward", std::move(rendering),
        {{texture, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [](rhi::IGraphicsCommandEncoder&) {});

    MockGraphicsEncoder encoder;
    graph.execute(encoder);

    const std::vector<std::string> expectedEvents{
        "label:Forward", "barriers:1", "beginRendering",
        "endRendering", "endLabel", "barriers:1"};
    if (encoder.events != expectedEvents || encoder.recordedBarriers.size() != 2 ||
        encoder.recordedBarriers[0].before != rhi::ResourceState::Undefined ||
        encoder.recordedBarriers[0].after != rhi::ResourceState::ColorAttachment ||
        encoder.recordedBarriers[1].before != rhi::ResourceState::ColorAttachment ||
        encoder.recordedBarriers[1].after != rhi::ResourceState::Present) {
        return 1;
    }
}
