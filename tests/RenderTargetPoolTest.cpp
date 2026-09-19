#include "render/render_target/RenderTargetPool.h"
#include "TestRenderDevice.h"

#include <gtest/gtest.h>

namespace engine {
namespace {

RenderTargetDesc targetDesc(std::string name) {
    RenderTargetDesc desc;
    desc.width = 320;
    desc.height = 180;
    desc.colorAttachments.emplace_back();
    desc.depthAttachment.emplace();
    desc.debugName = std::move(name);
    return desc;
}

TEST(RenderTargetPoolTest, RetiresResourcesAfterTheirFrameSlotCompletes) {
    MockDevice device;
    RenderTargetPool pool{device, 2};

    pool.beginFrame(0);
    const RenderTargetHandle first = pool.acquire(targetDesc("First"));
    ASSERT_TRUE(first);
    ASSERT_NE(pool.find(first), nullptr);
    EXPECT_EQ(pool.activeCount(), 1U);

    pool.release(first);
    EXPECT_EQ(pool.find(first), nullptr);
    EXPECT_EQ(pool.activeCount(), 0U);
    EXPECT_EQ(pool.retiredCount(), 1U);
    EXPECT_EQ(device.destroyedTextures, 0);

    pool.beginFrame(1);
    EXPECT_EQ(device.destroyedTextures, 0);
    pool.beginFrame(0);
    EXPECT_EQ(device.destroyedTextures, 2);
    EXPECT_EQ(device.destroyedViews, 2);
    EXPECT_EQ(pool.retiredCount(), 0U);
}

TEST(RenderTargetPoolTest, KeepsCameraTargetsIsolatedByHandle) {
    MockDevice device;
    RenderTargetPool pool{device, 2};
    pool.beginFrame(0);

    const RenderTargetHandle cameraA = pool.acquire(targetDesc("CameraA"));
    RenderTargetDesc cameraBDesc = targetDesc("CameraB");
    cameraBDesc.width = 1024;
    cameraBDesc.height = 512;
    const RenderTargetHandle cameraB = pool.acquire(std::move(cameraBDesc));

    ASSERT_TRUE(cameraA);
    ASSERT_TRUE(cameraB);
    EXPECT_NE(cameraA, cameraB);
    EXPECT_EQ(pool.find(cameraA)->width(), 320U);
    EXPECT_EQ(pool.find(cameraB)->width(), 1024U);

    pool.release(cameraA);
    EXPECT_EQ(pool.find(cameraA), nullptr);
    ASSERT_NE(pool.find(cameraB), nullptr);
    EXPECT_EQ(pool.find(cameraB)->height(), 512U);
}

} // namespace
} // namespace engine
