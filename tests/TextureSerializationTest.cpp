#include "asset/types/TextureAsset.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {
using namespace engine;

constexpr TextureFormat kAssetFormats[]{
    TextureFormat::Rgba8Unorm, TextureFormat::Rgba8Srgb,
    TextureFormat::Bgra8Unorm, TextureFormat::Bgra8Srgb,
    TextureFormat::R8Unorm, TextureFormat::Rg8Unorm,
    TextureFormat::R16Float, TextureFormat::Rg16Float, TextureFormat::Rgba16Float,
    TextureFormat::R32Float, TextureFormat::Rg32Float, TextureFormat::Rgba32Float,
    TextureFormat::Depth16Unorm, TextureFormat::Depth32Float, TextureFormat::R32Uint};

Ref<TextureAsset> makeTexture(TextureFormat format) {
    auto texture = makeRef<TextureAsset>();
    texture->desc = TextureDesc{TextureType::Texture2D, format, 5, 3, 3};
    texture->desc.sampler.filterMode = TextureFilterMode::Point;
    texture->desc.sampler.addressModeU = TextureAddressMode::ClampToEdge;
    TextureMipLayout layout;
    if (computeTextureLayout(texture->desc, layout)) {
        texture->pixels.resize(layout.totalSize);
        for (std::size_t i = 0; i < texture->pixels.size(); ++i)
            texture->pixels[i] = static_cast<std::uint8_t>((i * 37U + 19U) % 256U);
    }
    return texture;
}

void expectSamePayload(const TextureAsset& actual, const TextureAsset& expected) {
    EXPECT_EQ(actual.desc.type, expected.desc.type);
    EXPECT_EQ(actual.desc.format, expected.desc.format);
    EXPECT_EQ(actual.desc.width, expected.desc.width);
    EXPECT_EQ(actual.desc.height, expected.desc.height);
    EXPECT_EQ(actual.desc.depth, expected.desc.depth);
    EXPECT_EQ(actual.desc.arrayLayers, expected.desc.arrayLayers);
    EXPECT_EQ(actual.desc.mipCount, expected.desc.mipCount);
    EXPECT_EQ(actual.desc.sampler.filterMode, expected.desc.sampler.filterMode);
    EXPECT_EQ(actual.desc.sampler.addressModeU, expected.desc.sampler.addressModeU);
    EXPECT_EQ(actual.pixels, expected.pixels);
}

TEST(TextureSerializationTest, BinaryRoundTripPreservesFormatSamplerAndPixels) {
    for (const auto format : kAssetFormats) {
        const auto source = makeTexture(format);
        BinaryWriter writer;
        ASSERT_TRUE(source->transfer(writer));
        BinaryReader reader(writer.bytes());
        TextureAsset decoded;
        ASSERT_TRUE(decoded.transfer(reader));
        EXPECT_TRUE(reader.finished());
        expectSamePayload(decoded, *source);
    }
}

TEST(TextureSerializationTest, JsonRoundTripUsesVersionFiveWithoutColorSpaceField) {
    for (const auto format : kAssetFormats) {
        const auto source = makeTexture(format);
        JsonWriter writer;
        ASSERT_TRUE(source->transfer(writer));
        const auto text = writer.toString();
        const auto json = nlohmann::json::parse(text);
        EXPECT_EQ(json.at("version"), 5);
        EXPECT_FALSE(json.at("description").contains("color_space"));
        JsonReader reader(text);
        TextureAsset decoded;
        ASSERT_TRUE(decoded.transfer(reader));
        expectSamePayload(decoded, *source);
    }
}

TEST(TextureSerializationTest, JsonRejectsOldVersionsWithoutChangingTheDestination) {
    const auto source = makeTexture(TextureFormat::Rgba8Srgb);
    JsonWriter writer;
    ASSERT_TRUE(source->transfer(writer));
    auto json = nlohmann::json::parse(writer.toString());
    json["description"]["color_space"] = 1;
    for (const int version : {1, 2, 3, 4}) {
        json["version"] = version;
        JsonReader reader(json.dump());
        const auto destination = makeTexture(TextureFormat::Rgba8Unorm);
        EXPECT_FALSE(destination->transfer(reader));
        EXPECT_EQ(destination->desc.format, TextureFormat::Rgba8Unorm);
        EXPECT_EQ(destination->pixels, source->pixels);
    }
}

TEST(TextureSerializationTest, BinaryRejectsThePreviousColorSpaceLayout) {
    const auto source = makeTexture(TextureFormat::Rgba8Srgb);
    BinaryWriter writer;
    ASSERT_TRUE(source->transfer(writer));
    auto legacy = writer.takeBytes();
    // Binary objects have no markers: magic:u32, version:u16, type:i32, format:i32.
    // Version 4 then stored color_space:i32 before the dimensions.
    ASSERT_GT(legacy.size(), 14U);
    legacy[4] = std::byte{4};
    legacy[5] = std::byte{0};
    legacy.insert(legacy.begin() + 14, {std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}});
    BinaryReader reader(legacy);
    const auto destination = makeTexture(TextureFormat::Rgba8Unorm);
    EXPECT_FALSE(destination->transfer(reader));
    EXPECT_EQ(destination->desc.format, TextureFormat::Rgba8Unorm);
    EXPECT_EQ(destination->pixels, source->pixels);
}

TEST(TextureSerializationTest, RejectsMismatchedPayloadLengthsOnWriteAndRead) {
    for (auto format : kAssetFormats) {
        const auto source = makeTexture(format);
        JsonWriter writer;
        ASSERT_TRUE(source->transfer(writer));
        auto json = nlohmann::json::parse(writer.toString());
        json["description"]["width"] = 6;
        JsonReader reader(json.dump());
        const auto destination = makeTexture(TextureFormat::Rgba8Srgb);
        const auto previousPixels = destination->pixels;
        EXPECT_FALSE(destination->transfer(reader));
        EXPECT_EQ(destination->desc.format, TextureFormat::Rgba8Srgb);
        EXPECT_EQ(destination->desc.width, 5U);
        EXPECT_EQ(destination->desc.height, 3U);
        EXPECT_EQ(destination->desc.mipCount, 3U);
        EXPECT_EQ(destination->pixels, previousPixels);

        source->pixels.pop_back();
        BinaryWriter invalidWriter;
        EXPECT_FALSE(source->transfer(invalidWriter));
    }
}
} // namespace
