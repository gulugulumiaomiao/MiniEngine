#include "rhi/api/ResourceDesc.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {
using namespace engine::rhi;

static_assert(static_cast<int>(PixelFormat::Undefined) == 0);
static_assert(static_cast<int>(PixelFormat::Rgba8Unorm) == 1);
static_assert(static_cast<int>(PixelFormat::Rgba8Srgb) == 2);
static_assert(static_cast<int>(PixelFormat::Bgra8Unorm) == 3);
static_assert(static_cast<int>(PixelFormat::Bgra8Srgb) == 4);
static_assert(static_cast<int>(PixelFormat::Depth32Float) == 5);
static_assert(formatInfo(PixelFormat::Rgba8Unorm).bytesPerBlock == 4);
static_assert(isSrgbFormat(PixelFormat::Bgra8Srgb));
static_assert(isDepthFormat(PixelFormat::Depth32Float));

TEST(TextureFormatTest, ExistingColorFormatsDescribeStorageAndEncoding) {
    for (const auto format : {PixelFormat::Rgba8Unorm, PixelFormat::Rgba8Srgb,
                              PixelFormat::Bgra8Unorm, PixelFormat::Bgra8Srgb}) {
        SCOPED_TRACE(static_cast<int>(format));
        const auto info = formatInfo(format);
        EXPECT_TRUE(info.valid());
        EXPECT_EQ(info.blockWidth, 1U);
        EXPECT_EQ(info.blockHeight, 1U);
        EXPECT_EQ(info.bytesPerBlock, 4U);
        EXPECT_EQ(info.channelCount, 4U);
        EXPECT_EQ(info.numericType, FormatNumericType::Unorm);
        EXPECT_EQ(info.srgb, format == PixelFormat::Rgba8Srgb || format == PixelFormat::Bgra8Srgb);
        EXPECT_TRUE(isColorFormat(format));
        EXPECT_FALSE(isDepthFormat(format));
        EXPECT_FALSE(hasStencil(format));
    }
}

TEST(TextureFormatTest, DepthIsFloatDataAndNeverSrgbColor) {
    const auto info = formatInfo(PixelFormat::Depth32Float);
    EXPECT_TRUE(info.valid());
    EXPECT_EQ(info.blockWidth, 1U);
    EXPECT_EQ(info.blockHeight, 1U);
    EXPECT_EQ(info.bytesPerBlock, 4U);
    EXPECT_EQ(info.channelCount, 1U);
    EXPECT_EQ(info.numericType, FormatNumericType::Float);
    EXPECT_TRUE(isDepthFormat(PixelFormat::Depth32Float));
    EXPECT_FALSE(isColorFormat(PixelFormat::Depth32Float));
    EXPECT_FALSE(isSrgbFormat(PixelFormat::Depth32Float));
    EXPECT_FALSE(hasStencil(PixelFormat::Depth32Float));
}

TEST(TextureFormatTest, UndefinedAndUnknownFormatsHaveNoStorageOrClassification) {
    for (const auto format : {PixelFormat::Undefined, static_cast<PixelFormat>(-1),
                              static_cast<PixelFormat>(1000)}) {
        const auto info = formatInfo(format);
        EXPECT_FALSE(info.valid());
        EXPECT_EQ(info.blockWidth, 0U);
        EXPECT_EQ(info.blockHeight, 0U);
        EXPECT_EQ(info.bytesPerBlock, 0U);
        EXPECT_EQ(info.channelCount, 0U);
        EXPECT_EQ(info.numericType, FormatNumericType::Undefined);
        EXPECT_FALSE(isColorFormat(format));
        EXPECT_FALSE(isDepthFormat(format));
        EXPECT_FALSE(isSrgbFormat(format));
        EXPECT_FALSE(hasStencil(format));
    }
}
} // namespace
