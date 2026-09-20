#include <gtest/gtest.h>

#include <cstring>

#include "render/global_uniform/GlobalUniformManager.h"
#include "render/shader/Shader.h"

using namespace engine;

class GlobalUniformTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset the singleton state before each test.
        GLOBAL_UNIFORM_MANAGER.clear();
    }
};

TEST_F(GlobalUniformTest, LayoutMergesNumericGlobals) {
    std::vector<ShaderPropertyDesc> properties;
    {
        ShaderPropertyDesc p;
        p.name = "_GlobalTint";
        p.type = ShaderPropertyType::Color;
        p.defaultValue = math::Vec4{1.0F, 0.0F, 0.0F, 1.0F};
        properties.push_back(p);
    }
    {
        ShaderPropertyDesc p;
        p.name = "_GlobalScale";
        p.type = ShaderPropertyType::Float;
        p.defaultValue = 2.0F;
        properties.push_back(p);
    }

    GLOBAL_UNIFORM_MANAGER.registerGlobalProperties(properties);

    const UniformBlockLayout& layout = GLOBAL_UNIFORM_MANAGER.uniformBlockLayout();
    ASSERT_EQ(layout.members.size(), 2u);
    EXPECT_EQ(layout.members[0].name, "_GlobalScale");
    EXPECT_EQ(layout.members[0].type, ShaderPropertyType::Float);
    EXPECT_EQ(layout.members[0].offset, 0u);
    EXPECT_EQ(layout.members[1].name, "_GlobalTint");
    EXPECT_EQ(layout.members[1].type, ShaderPropertyType::Color);
    EXPECT_EQ(layout.members[1].offset, 16u);
    EXPECT_EQ(layout.byteSize, 32u);
}

TEST_F(GlobalUniformTest, SetValueUpdatesBuffer) {
    std::vector<ShaderPropertyDesc> properties;
    {
        ShaderPropertyDesc p;
        p.name = "_GlobalTint";
        p.type = ShaderPropertyType::Color;
        p.defaultValue = math::Vec4{1.0F, 0.0F, 0.0F, 1.0F};
        properties.push_back(p);
    }
    GLOBAL_UNIFORM_MANAGER.registerGlobalProperties(properties);

    const std::uint64_t versionBefore = GLOBAL_UNIFORM_MANAGER.version();
    Shader::setGlobalColor("_GlobalTint", math::Vec4{0.0F, 1.0F, 0.0F, 1.0F});
    EXPECT_GT(GLOBAL_UNIFORM_MANAGER.version(), versionBefore);

    const UniformBlockLayout& layout = GLOBAL_UNIFORM_MANAGER.uniformBlockLayout();
    const auto* member = layout.findMember("_GlobalTint");
    ASSERT_NE(member, nullptr);

    std::span<const std::byte> bytes = GLOBAL_UNIFORM_MANAGER.uniformBytes();
    ASSERT_EQ(bytes.size(), layout.byteSize);
    math::Vec4 value;
    std::memcpy(&value, bytes.data() + member->offset, sizeof(math::Vec4));
    EXPECT_FLOAT_EQ(value.x, 0.0F);
    EXPECT_FLOAT_EQ(value.y, 1.0F);
    EXPECT_FLOAT_EQ(value.z, 0.0F);
    EXPECT_FLOAT_EQ(value.w, 1.0F);
}

TEST_F(GlobalUniformTest, MatrixLayoutAndBuffer) {
    std::vector<ShaderPropertyDesc> properties;
    {
        ShaderPropertyDesc p;
        p.name = "_GlobalMatrix";
        p.type = ShaderPropertyType::Matrix;
        p.defaultValue = math::Mat44{1.0F};
        properties.push_back(p);
    }
    GLOBAL_UNIFORM_MANAGER.registerGlobalProperties(properties);

    const UniformBlockLayout& layout = GLOBAL_UNIFORM_MANAGER.uniformBlockLayout();
    ASSERT_EQ(layout.members.size(), 1u);
    EXPECT_EQ(layout.members[0].name, "_GlobalMatrix");
    EXPECT_EQ(layout.members[0].type, ShaderPropertyType::Matrix);
    EXPECT_EQ(layout.members[0].size, 64u);
    EXPECT_EQ(layout.members[0].alignment, 16u);
    EXPECT_EQ(layout.byteSize, 64u);

    const std::uint64_t versionBefore = GLOBAL_UNIFORM_MANAGER.version();
    math::Mat44 value{2.0F};
    value[0][3] = 3.0F;
    Shader::setGlobalMatrix("_GlobalMatrix", value);
    EXPECT_GT(GLOBAL_UNIFORM_MANAGER.version(), versionBefore);

    std::span<const std::byte> bytes = GLOBAL_UNIFORM_MANAGER.uniformBytes();
    ASSERT_EQ(bytes.size(), layout.byteSize);
    math::Mat44 readValue;
    std::memcpy(&readValue, bytes.data() + layout.members[0].offset, sizeof(math::Mat44));
    EXPECT_FLOAT_EQ(readValue[0][0], 2.0F);
    EXPECT_FLOAT_EQ(readValue[0][3], 3.0F);
}

TEST_F(GlobalUniformTest, TexturePropertiesAreTrackedSeparately) {
    std::vector<ShaderPropertyDesc> properties;
    {
        ShaderPropertyDesc p;
        p.name = "_GlobalTex";
        p.type = ShaderPropertyType::Texture2D;
        p.defaultValue = std::string{"assets://textures/white.png"};
        properties.push_back(p);
    }
    GLOBAL_UNIFORM_MANAGER.registerGlobalProperties(properties);

    EXPECT_EQ(GLOBAL_UNIFORM_MANAGER.uniformBlockLayout().members.size(), 0u);
    ASSERT_EQ(GLOBAL_UNIFORM_MANAGER.textureProperties().size(), 1u);
    EXPECT_EQ(GLOBAL_UNIFORM_MANAGER.textureProperties()[0].name, "_GlobalTex");

    Shader::setGlobalTexture("_GlobalTex", "assets://textures/noise.png");
    const std::string* reference = GLOBAL_UNIFORM_MANAGER.findTexture("_GlobalTex");
    ASSERT_NE(reference, nullptr);
    EXPECT_EQ(*reference, "assets://textures/noise.png");
}
