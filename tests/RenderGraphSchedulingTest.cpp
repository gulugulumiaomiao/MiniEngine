#include "TestRenderDevice.h"
#include "render/render_graph/RenderGraph.h"

#include <gtest/gtest.h>

namespace engine {
namespace {

RgTextureHandle makeTransient(RenderGraph& graph, std::string name) {
    return graph.createTexture({
        .format = rhi::TextureFormat::Rgba8Unorm,
        .width = 64,
        .height = 64,
        .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled,
        .debugName = std::move(name),
    });
}

RgRenderingInfo colorOutput(RgTextureHandle texture) {
    RgRenderingInfo rendering;
    rendering.renderArea = {0, 0, 64, 64};
    rendering.colorAttachments.push_back(
        {texture, rhi::LoadOp::Clear, rhi::StoreOp::Store, {}});
    return rendering;
}

TEST(RenderGraphSchedulingTest, OrdersPassesFromTextureDependencies) {
    MockDevice device;
    RgTexturePool pool{device, 2};
    pool.beginFrame(0);
    RenderGraph graph;
    const RgTextureHandle gbuffer = makeTransient(graph, "SchedulingGBuffer");
    const RgTextureHandle output = makeTransient(graph, "SchedulingOutput");

    graph.addGraphicsPass(
        "Composite",
        colorOutput(output),
        {{gbuffer, rhi::TextureAspect::Color, rhi::ResourceState::ShaderRead},
         {output, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [](rhi::IGraphicsCommandEncoder&) {});
    graph.addGraphicsPass(
        "GBuffer",
        colorOutput(gbuffer),
        {{gbuffer, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
        [](rhi::IGraphicsCommandEncoder&) {});

    ASSERT_TRUE(graph.compile(pool)) << graph.lastError();
    EXPECT_EQ(graph.executionOrder(), (std::vector<std::string>{"GBuffer", "Composite"}));
}

TEST(RenderGraphSchedulingTest, RejectsDependencyCyclesWithDiagnostic) {
    MockDevice device;
    RgTexturePool pool{device, 2};
    pool.beginFrame(0);
    RenderGraph graph;
    const RgTextureHandle a = makeTransient(graph, "CycleA");
    const RgTextureHandle b = makeTransient(graph, "CycleB");

    graph.addGraphicsPass(
        "CyclePassA",
        colorOutput(a),
        {{a, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
         {b, rhi::TextureAspect::Color, rhi::ResourceState::ShaderRead}},
        [](rhi::IGraphicsCommandEncoder&) {});
    graph.addGraphicsPass(
        "CyclePassB",
        colorOutput(b),
        {{b, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment},
         {a, rhi::TextureAspect::Color, rhi::ResourceState::ShaderRead}},
        [](rhi::IGraphicsCommandEncoder&) {});

    EXPECT_FALSE(graph.compile(pool));
    EXPECT_FALSE(graph.compiled());
    EXPECT_NE(graph.lastError().find("cycle"), std::string::npos);
    EXPECT_EQ(pool.inUseCount(), 0U);
}

TEST(RenderGraphSchedulingTest, ReusesAPlanForAnUnchangedGraphShape) {
    MockDevice device;
    RgTexturePool pool{device, 2};
    pool.beginFrame(0);

    const auto compileGraph = [&](bool& cacheHit) {
        RenderGraph graph;
        const RgTextureHandle texture = makeTransient(graph, "CachedPlanTexture");
        graph.addGraphicsPass(
            "CachedPlanWriter",
            colorOutput(texture),
            {{texture, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment}},
            [](rhi::IGraphicsCommandEncoder&) {});
        EXPECT_TRUE(graph.compile(pool)) << graph.lastError();
        cacheHit = graph.planCacheHit();
    };

    bool firstHit{};
    bool secondHit{};
    compileGraph(firstHit);
    compileGraph(secondHit);
    EXPECT_TRUE(secondHit);
}

} // namespace
} // namespace engine
