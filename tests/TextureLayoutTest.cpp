#include "render/texture/Texture.h"

#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace engine;

TEST(TextureLayoutTest, OddMipChainsUseFormatByteSize) {
    for (auto format : {TextureFormat::R8Unorm, TextureFormat::Rg8Unorm,
                       TextureFormat::Rgba16Float, TextureFormat::R32Float,
                       TextureFormat::Rgba32Float, TextureFormat::Depth16Unorm, TextureFormat::R32Uint}) {
        const auto bytes = rhi::formatInfo(toRhi(format)).bytesPerBlock;
        TextureMipLayout layout;
        ASSERT_TRUE(computeTextureLayout(TextureDesc{TextureType::Texture2D, format, 5, 3, 3}, layout));
        EXPECT_EQ(layout.offsets, (std::vector<std::size_t>{0, 15U * bytes, 17U * bytes}));
        EXPECT_EQ(layout.sizes, (std::vector<std::size_t>{15U * bytes, 2U * bytes, bytes}));
        EXPECT_EQ(layout.totalSize, 18U * bytes);
    }
}

TEST(TextureLayoutTest, StagingAlignmentDoesNotChangeMipPayloadSize) {
    TextureMipLayout layout;
    ASSERT_TRUE(rhi::computeTextureLayout(rhi::PixelFormat::R8Unorm, 5, 3, 3, layout, 4));
    EXPECT_EQ(layout.offsets, (std::vector<std::size_t>{0, 16, 20}));
    EXPECT_EQ(layout.sizes, (std::vector<std::size_t>{15, 2, 1}));
    EXPECT_EQ(layout.totalSize, 21U);
    ASSERT_TRUE(rhi::computeTextureLayout(rhi::PixelFormat::Rg8Unorm, 5, 3, 3, layout, 4));
    EXPECT_EQ(layout.offsets, (std::vector<std::size_t>{0, 32, 36}));
    EXPECT_EQ(layout.totalSize, 38U);
}

TEST(TextureLayoutTest, RejectsInvalidAndOverflowingLayoutsWithoutStaleOutput) {
    const auto max = std::numeric_limits<std::uint32_t>::max();
    const auto sizeMax = std::numeric_limits<std::size_t>::max();
    TextureMipLayout layout;
    auto reject = [&](rhi::PixelFormat format, std::uint32_t w, std::uint32_t h,
                      std::uint32_t mips, std::size_t alignment = 1) {
        layout = {{123}, {456}, 789};
        EXPECT_FALSE(rhi::computeTextureLayout(format, w, h, mips, layout, alignment));
        EXPECT_TRUE(layout.offsets.empty());
        EXPECT_TRUE(layout.sizes.empty());
        EXPECT_EQ(layout.totalSize, 0U);
    };
    reject(rhi::PixelFormat::Undefined, 1, 1, 1);
    reject(static_cast<rhi::PixelFormat>(999), 1, 1, 1);
    reject(rhi::PixelFormat::R8Unorm, 0, 1, 1);
    reject(rhi::PixelFormat::R8Unorm, 1, 0, 1);
    reject(rhi::PixelFormat::R8Unorm, 1, 1, 0);
    reject(rhi::PixelFormat::R8Unorm, 5, 3, max);
    reject(rhi::PixelFormat::R8Unorm, 1, 1, 1, 0);
    reject(rhi::PixelFormat::Rgba32Float, max, max, 1);
    reject(rhi::PixelFormat::R8Unorm, max, max, 32);
    reject(rhi::PixelFormat::R8Unorm, 2, 2, 2, sizeMax);
    std::size_t bytes = 123;
    EXPECT_FALSE(rhi::computeTextureByteSize(rhi::PixelFormat::Rgba32Float, max, max, max, bytes));
    EXPECT_EQ(bytes, 0U);
}

TEST(TextureLayoutTest, EngineLayoutRejectsUnsupportedShapesAndSamples) {
    TextureDesc desc{TextureType::Texture2D, TextureFormat::R8Unorm, 5, 3, 3};
    TextureMipLayout layout;
    desc.type = TextureType::Texture3D;
    EXPECT_FALSE(computeTextureLayout(desc, layout));
    desc.type = TextureType::Texture2D;
    desc.arrayLayers = 2;
    EXPECT_FALSE(computeTextureLayout(desc, layout));
    desc.arrayLayers = 1;
    desc.depth = 2;
    EXPECT_FALSE(computeTextureLayout(desc, layout));
    desc.depth = 1;
    desc.samples = rhi::SampleCount::Four;
    EXPECT_FALSE(computeTextureLayout(desc, layout));
}
} // namespace
