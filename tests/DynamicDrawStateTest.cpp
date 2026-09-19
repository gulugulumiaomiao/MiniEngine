#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "render/renderer/DrawBatcher.h"

#include <gtest/gtest.h>

#include <array>

namespace engine {
namespace {

TEST(DynamicDrawStateTest, ShaderPassStateResolvesToRhiDrawState) {
    ShaderPassDesc passDesc;
    RenderStateDesc state;
    state.cull = CullMode::Front;
    state.frontFace = FrontFace::CounterClockwise;
    state.depthWrite = false;
    state.depthTest = DepthCompare::GreaterEqual;
    state.blend = BlendMode::PremultipliedAlpha;
    state.colorMask = "RGB";
    const ShaderPass pass{passDesc, state};

    const rhi::DrawStateDesc resolved = GraphicsPipelineManager::makeDrawState(pass);
    EXPECT_EQ(resolved.raster.cull, rhi::CullMode::Front);
    EXPECT_EQ(resolved.raster.frontFace, rhi::FrontFace::CounterClockwise);
    EXPECT_TRUE(resolved.depthStencil.depthTestEnable);
    EXPECT_FALSE(resolved.depthStencil.depthWriteEnable);
    EXPECT_EQ(resolved.depthStencil.depthCompare, rhi::CompareOp::GreaterEqual);
    EXPECT_EQ(resolved.blend.mode, rhi::BlendMode::PremultipliedAlpha);
    EXPECT_TRUE(rhi::hasFlag(resolved.blend.colorWriteMask, rhi::ColorWriteMask::Blue));
    EXPECT_FALSE(rhi::hasFlag(resolved.blend.colorWriteMask, rhi::ColorWriteMask::Alpha));
}

TEST(DynamicDrawStateTest, DifferentDynamicStateSplitsBatches) {
    DrawItem first;
    first.pipeline = {1, 1};
    first.materialBindGroup = {2, 1};
    first.indexBuffer = {3, 1};
    first.arguments = {.indexCount = 3, .instanceCount = 1};

    DrawItem second = first;
    second.arguments.firstInstance = 1;
    second.drawState.blend.mode = rhi::BlendMode::Alpha;

    const std::array items{first, second};
    const BatchedDrawList result = DrawBatcher{}.build(items);
    ASSERT_EQ(result.batches.size(), 2U);
    EXPECT_EQ(result.batches[0].drawState.blend.mode, rhi::BlendMode::Off);
    EXPECT_EQ(result.batches[1].drawState.blend.mode, rhi::BlendMode::Alpha);
}

} // namespace
} // namespace engine
