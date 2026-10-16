#include "asset/types/TextureAsset.h"
#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {
using namespace engine;

Ref<TextureAsset> makeTexture(TextureFormat format) {
    auto texture = makeRef<TextureAsset>();
    texture->desc = TextureDesc{TextureType::Texture2D, format, 2, 1};
    texture->desc.sampler.filterMode = TextureFilterMode::Point;
    texture->desc.sampler.addressModeU = TextureAddressMode::ClampToEdge;
    texture->pixels = {10, 20, 30, 255, 40, 50, 60, 128};
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
    for (const auto format : {TextureFormat::Rgba8Unorm, TextureFormat::Rgba8Srgb}) {
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
    for (const auto format : {TextureFormat::Rgba8Unorm, TextureFormat::Rgba8Srgb}) {
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
} // namespace
