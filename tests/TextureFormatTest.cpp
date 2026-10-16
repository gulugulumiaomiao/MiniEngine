#include "rhi/api/ResourceDesc.h"
#include "rhi/vulkan/VulkanConversions.h"
#include "render/texture/Texture.h"

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

TEST(TextureFormatTest, ExtendedFormatsMapAcrossEngineRhiAndVulkan) {
    struct FormatCase {
        engine::TextureFormat engineFormat;
        PixelFormat format;
        VkFormat vulkanFormat;
        std::uint32_t bytes, channels;
        FormatNumericType numericType;
        bool depth;
    };
    const FormatCase cases[]{
        {engine::TextureFormat::R32Uint, PixelFormat::R32Uint, VK_FORMAT_R32_UINT,
            4, 1, FormatNumericType::Uint, false},
        {engine::TextureFormat::R8Unorm, PixelFormat::R8Unorm, VK_FORMAT_R8_UNORM,
            1, 1, FormatNumericType::Unorm, false},
        {engine::TextureFormat::Rg8Unorm, PixelFormat::Rg8Unorm, VK_FORMAT_R8G8_UNORM,
            2, 2, FormatNumericType::Unorm, false},
        {engine::TextureFormat::R16Float, PixelFormat::R16Float, VK_FORMAT_R16_SFLOAT,
            2, 1, FormatNumericType::Float, false},
        {engine::TextureFormat::Rg16Float, PixelFormat::Rg16Float, VK_FORMAT_R16G16_SFLOAT,
            4, 2, FormatNumericType::Float, false},
        {engine::TextureFormat::Rgba16Float, PixelFormat::Rgba16Float, VK_FORMAT_R16G16B16A16_SFLOAT,
            8, 4, FormatNumericType::Float, false},
        {engine::TextureFormat::R32Float, PixelFormat::R32Float, VK_FORMAT_R32_SFLOAT,
            4, 1, FormatNumericType::Float, false},
        {engine::TextureFormat::Rg32Float, PixelFormat::Rg32Float, VK_FORMAT_R32G32_SFLOAT,
            8, 2, FormatNumericType::Float, false},
        {engine::TextureFormat::Rgba32Float, PixelFormat::Rgba32Float, VK_FORMAT_R32G32B32A32_SFLOAT,
            16, 4, FormatNumericType::Float, false},
        {engine::TextureFormat::Depth16Unorm, PixelFormat::Depth16Unorm, VK_FORMAT_D16_UNORM,
            2, 1, FormatNumericType::Unorm, true},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(static_cast<int>(test.engineFormat));
        EXPECT_EQ(engine::toRhi(test.engineFormat), test.format);
        EXPECT_EQ(vulkan::toVulkan(test.format), test.vulkanFormat);
        EXPECT_EQ(vulkan::toRhi(test.vulkanFormat), test.format);
        const auto info = formatInfo(test.format);
        EXPECT_TRUE(info.valid());
        EXPECT_EQ(info.blockWidth, 1U);
        EXPECT_EQ(info.blockHeight, 1U);
        EXPECT_EQ(info.bytesPerBlock, test.bytes);
        EXPECT_EQ(info.channelCount, test.channels);
        EXPECT_EQ(info.numericType, test.numericType);
        EXPECT_EQ(isDepthFormat(test.format), test.depth);
        EXPECT_EQ(isColorFormat(test.format), !test.depth);
        EXPECT_FALSE(info.srgb);
        EXPECT_FALSE(info.stencil);
    }
    // Preserve the values already used by serialized engine texture descriptions.
    EXPECT_EQ(static_cast<int>(engine::TextureFormat::Rgba8Unorm), 0);
    EXPECT_EQ(static_cast<int>(engine::TextureFormat::Rgba8Srgb), 1);
    EXPECT_EQ(static_cast<int>(engine::TextureFormat::Depth32Float), 2);
    EXPECT_EQ(static_cast<int>(engine::TextureFormat::Bgra8Unorm), 3);
    EXPECT_EQ(static_cast<int>(engine::TextureFormat::Bgra8Srgb), 4);
    EXPECT_EQ(vulkan::toRhi(VK_FORMAT_D32_SFLOAT), PixelFormat::Depth32Float);
}
} // namespace
