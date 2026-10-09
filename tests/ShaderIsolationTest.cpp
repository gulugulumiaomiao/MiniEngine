#include "asset/types/ShaderAsset.h"
#include "asset/derived_data/AssetArtifact.h"
#include "asset/types/MaterialAsset.h"
#include "core/serialization/BinaryTransfer.h"
#include "render/global_uniform/GlobalUniformManager.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace {

using namespace engine;

static_assert(!std::is_same_v<ShaderPropertyAssetData, ShaderPropertyInfo>);
static_assert(!std::is_same_v<ShaderPassAssetDesc, ShaderPassCreateInfo>);
static_assert(!std::is_same_v<ShaderRenderStateAssetData, RenderState>);
static_assert(!std::is_same_v<ShaderAssetPropertyType, ShaderPropertyType>);
static_assert(!std::is_base_of_v<Transferable, ShaderPropertyInfo>);
static_assert(!std::is_base_of_v<Transferable, ShaderInterfaceVariable>);
static_assert(!std::is_base_of_v<Transferable, RenderState>);

ShaderAssetData assetData() {
    ShaderAssetData data;
    data.name = "Tests/Isolation";
    ShaderPropertyAssetData range;
    range.name = "Roughness";
    range.displayName = "Surface Roughness";
    range.type = ShaderAssetPropertyType::Range;
    range.defaultValue = 0.5F;
    range.range = math::Vec2{0.0F, 1.0F};
    range.attributes = {"EditorSlider"};
    data.properties.push_back(range);
    ShaderPropertyAssetData color;
    color.name = "Tint";
    color.type = ShaderAssetPropertyType::Color;
    color.defaultValue = math::Vec4{1.0F, 0.5F, 0.25F, 1.0F};
    data.properties.push_back(color);
    ShaderPropertyAssetData global;
    global.name = "IsolationGlobal";
    global.defaultValue = 2.0F;
    data.globalProperties.push_back(global);
    ShaderPassAssetData pass;
    pass.pass.name = "Forward";
    pass.pass.program.vertexSource = VirtualPath{"assets://isolation.vert"};
    pass.pass.program.fragmentSource = VirtualPath{"assets://isolation.frag"};
    pass.pass.features = {"FEATURE_B", "FEATURE_A", "FEATURE_A"};
    pass.renderState.cull = ShaderAssetCullMode::Front;
    SubShaderAssetData sub;
    sub.passes.push_back(pass);
    data.subShaders.push_back(sub);
    return data;
}

class ShaderIsolationTest : public ::testing::Test {
protected:
    void SetUp() override { GLOBAL_UNIFORM_MANAGER.clear(); }
    void TearDown() override { GLOBAL_UNIFORM_MANAGER.clear(); }
};

TEST_F(ShaderIsolationTest, ConvertsAssetMetadataToRuntimeTypes) {
    ShaderAsset asset;
    asset.setAssetPath(VirtualPath{"assets://isolation.shader.json"});
    asset.data = assetData();
    const auto info = asset.buildRuntimeInfo();
    ASSERT_TRUE(info);
    ASSERT_EQ(info->properties.size(), 2U);
    EXPECT_EQ(info->properties[0].type, ShaderPropertyType::Float);
    EXPECT_FLOAT_EQ(std::get<float>(info->properties[0].defaultValue), 0.5F);
    EXPECT_EQ(info->properties[1].type, ShaderPropertyType::Vec4);
    EXPECT_EQ(info->subShaders[0].passes[0].renderState.cull, CullMode::Front);
    EXPECT_EQ(info->compileKey, hashString("assets://isolation.shader.json"));
    auto shader = asset.instantiate();
    ASSERT_TRUE(shader);
    EXPECT_EQ(shader->uniformBlockLayout().requireMember("Tint").offset, 16U);
    EXPECT_EQ(shader->globalUniformBlockLayout().byteSize, 16U);
    const auto& schema = shader->defaultSubShader().passes()[0].keyword();
    EXPECT_EQ(schema.keywords(), (std::vector<std::string>{"FEATURE_A", "FEATURE_B"}));
    EXPECT_EQ(asset.data.properties[0].displayName, "Surface Roughness");
    EXPECT_TRUE(asset.data.properties[0].range.has_value());
}

TEST_F(ShaderIsolationTest, AssetRetainsInstanceAndRuntimeSurvivesAssetDestruction) {
    Ref<Shader> shader;
    std::uint64_t id{};
    {
        auto asset = makeRef<ShaderAsset>();
        asset->data = assetData();
        shader = asset->instantiate();
        ASSERT_TRUE(shader);
        id = shader->instanceId();
        EXPECT_EQ(shader->refCount(), 2U);
        shader.reset();
        ASSERT_TRUE(asset->runtimeInstance());
        EXPECT_EQ(asset->runtimeInstance()->refCount(), 1U);
        shader = asset->instantiate();
        EXPECT_EQ(shader->instanceId(), id);
    }
    EXPECT_EQ(shader->refCount(), 1U);
    EXPECT_EQ(shader->name(), "Tests/Isolation");
    auto clone = shader->clone();
    ASSERT_TRUE(clone);
    EXPECT_NE(clone->instanceId(), id);
    EXPECT_EQ(clone->compileKey(), shader->compileKey());
    ASSERT_TRUE(clone->clone());
}

TEST_F(ShaderIsolationTest, RuntimeCanBeConstructedWithoutAsset) {
    ShaderCreateInfo info;
    info.name = "Procedural";
    info.properties.push_back({"Value", ShaderPropertyType::Float, 3.0F});
    ShaderPassCreateInfo pass;
    pass.name = "Forward";
    pass.program = {VirtualPath{"memory://test.vert"}, VirtualPath{"memory://test.frag"}};
    SubShaderCreateInfo sub;
    sub.passes.push_back(pass);
    info.subShaders.push_back(sub);
    auto shader = makeRef<Shader>(info);
    EXPECT_EQ(shader->uniformBlockLayout().byteSize, 16U);
    const auto revision = shader->revision();
    info.properties[0].defaultValue = false; // Invalid float default.
    EXPECT_FALSE(shader->rebuild(info));
    EXPECT_EQ(shader->revision(), revision);
    EXPECT_FLOAT_EQ(std::get<float>(shader->properties()[0].defaultValue), 3.0F);
}

TEST_F(ShaderIsolationTest, ReloadUpdatesGlobalsAndPreservesDetachedSnapshot) {
    ShaderAsset asset;
    asset.data = assetData();
    auto shader = asset.instantiate();
    auto clone = shader->clone();
    const auto revision = shader->revision();
    const auto id = shader->instanceId();
    ShaderAsset updated;
    updated.data = assetData();
    updated.data.name = "Reloaded";
    updated.data.properties[0].defaultValue = 0.75F;
    updated.data.globalProperties[0].name = "ReplacementGlobal";
    BinaryWriter writer;
    ASSERT_TRUE(updated.transfer(writer));
    BinaryReader reader{writer.bytes()};
    ASSERT_TRUE(asset.transfer(reader));
    ASSERT_TRUE(reader.finished());
    EXPECT_EQ(asset.instantiate(), shader);
    EXPECT_EQ(shader->instanceId(), id);
    EXPECT_EQ(shader->revision(), revision + 1);
    EXPECT_EQ(shader->name(), "Reloaded");
    EXPECT_EQ(asset.data.globalProperties[0].name, "ReplacementGlobal");
    EXPECT_EQ(shader->globalProperties()[0].name, "ReplacementGlobal");
    EXPECT_EQ(clone->name(), "Tests/Isolation");
    EXPECT_FLOAT_EQ(std::get<float>(clone->properties()[0].defaultValue), 0.5F);
    // Repeated cloning copies the runtime snapshot, independent of current asset data.
    EXPECT_EQ(clone->clone()->name(), "Tests/Isolation");
}

TEST_F(ShaderIsolationTest, InvalidReloadPreservesAssetAndRuntime) {
    ShaderAsset asset;
    asset.data = assetData();
    auto shader = asset.instantiate();
    const auto revision = shader->revision();
    ShaderAsset invalid;
    invalid.data = assetData();
    invalid.data.name = "Invalid";
    invalid.data.properties[0].defaultValue = false;
    BinaryWriter writer;
    ASSERT_TRUE(invalid.transfer(writer));
    BinaryReader reader{writer.bytes()};
    EXPECT_FALSE(asset.transfer(reader));
    EXPECT_EQ(asset.data.name, "Tests/Isolation");
    EXPECT_EQ(shader->name(), "Tests/Isolation");
    EXPECT_EQ(shader->revision(), revision);
}

TEST_F(ShaderIsolationTest, MaterialPreservesCompatibleValuesAcrossLayoutAndTypeChanges) {
    ShaderAsset asset;
    asset.data = assetData();
    const auto shader = asset.instantiate();
    MaterialAsset materialAsset;
    materialAsset.data.properties.emplace("Roughness", 0.9F);
    const math::Vec4 tint{0.1F, 0.2F, 0.3F, 1.0F};
    materialAsset.data.properties.emplace("Tint", tint);
    const auto material = makeRef<Material>(*materialAsset.buildRuntimeInfo(shader));
    ASSERT_TRUE(material);

    auto info = *asset.buildRuntimeInfo();
    info.properties.insert(info.properties.begin(),
                           {"Extra", ShaderPropertyType::Vec4, math::Vec4{2.0F}});
    ASSERT_TRUE(shader->rebuild(info));
    material->setShader(shader);
    EXPECT_FLOAT_EQ(material->getFloat("Roughness"), 0.9F);
    EXPECT_EQ(material->getVec4("Tint"), tint);
    EXPECT_EQ(material->getVec4("Extra"), math::Vec4{2.0F});

    info.properties[1].type = ShaderPropertyType::Vec2;
    info.properties[1].defaultValue = math::Vec2{0.25F, 0.75F};
    ASSERT_TRUE(shader->rebuild(info));
    material->setShader(shader);
    EXPECT_EQ(material->getVec2("Roughness"), (math::Vec2{0.25F, 0.75F}));
    EXPECT_EQ(material->getVec4("Tint"), tint);
}

TEST_F(ShaderIsolationTest, KeywordMaskUsesAll64BitsAndIgnoresUnknownAndDuplicateWords) {
    std::vector<std::string> declared;
    for (int index = 0; index < 64; ++index)
        declared.push_back("KEY_" + std::string(index < 10 ? "0" : "") + std::to_string(index));
    const ShaderKeywordSchema schema{declared};
    const std::vector<std::string> endpoints{"KEY_00", "KEY_63", "KEY_63", "UNKNOWN"};
    EXPECT_EQ(schema.makeKey(endpoints), (std::uint64_t{1} | (std::uint64_t{1} << 63)));
    EXPECT_EQ(schema.makeKey(declared), std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(schema.makeKey({}), 0U);
    EXPECT_EQ(ShaderKeywordSchema{}.makeKey(endpoints), 0U);
    static_assert(std::is_same_v<decltype(schema.makeKey(endpoints)), std::uint64_t>);
}

TEST_F(ShaderIsolationTest, MaterialSelectsKeywordsIndependentlyForEachPass) {
    ShaderAsset asset;
    asset.data = assetData();
    auto second = asset.data.subShaders[0].passes[0];
    second.pass.name = "Shadow";
    second.pass.type = ShaderAssetPassType::ShadowCaster;
    second.pass.features = {"FEATURE_B", "SHADOW_ONLY"};
    asset.data.subShaders[0].passes.push_back(second);
    const auto shader = asset.instantiate();
    MaterialAsset materialAsset;
    const auto material = makeRef<Material>(*materialAsset.buildRuntimeInfo(shader));
    const auto other = makeRef<Material>(*materialAsset.buildRuntimeInfo(shader));
    ASSERT_TRUE(material);
    ASSERT_TRUE(other);
    const auto& forward = shader->defaultSubShader().passes()[0];
    const auto& shadow = shader->defaultSubShader().passes()[1];
    material->enableKeyword("FEATURE_A");
    material->enableKeyword("SHADOW_ONLY");
    EXPECT_TRUE(material->isKeywordEnabled("FEATURE_A"));
    EXPECT_EQ(material->variantKey(forward), 1U);
    EXPECT_EQ(material->variantKey(shadow), 2U);
    EXPECT_EQ(other->variantKey(forward), 0U);
    EXPECT_EQ(other->variantKey(shadow), 0U);
    const auto version = material->version();
    material->enableKeyword("FEATURE_A");
    EXPECT_EQ(material->version(), version);
    material->enableKeyword("UNDECLARED");
    EXPECT_EQ(material->version(), version + 1);
    EXPECT_TRUE(material->isKeywordEnabled("UNDECLARED"));
    EXPECT_EQ(material->variantKey(forward), 1U);
    EXPECT_EQ(material->variantKey(shadow), 2U);
    material->disableKeyword("FEATURE_A");
    EXPECT_EQ(material->variantKey(forward), 0U);
    EXPECT_EQ(material->variantKey(shadow), 2U);
    material->enableKeyword("FEATURE_B");
    EXPECT_EQ(material->variantKey(forward), 2U);
    EXPECT_EQ(material->variantKey(shadow), 3U);
    auto changed = *asset.buildRuntimeInfo();
    changed.subShaders[0].passes[0].features.clear();
    ASSERT_TRUE(shader->rebuild(changed));
    material->setShader(shader);
    EXPECT_TRUE(material->isKeywordEnabled("UNDECLARED"));
    EXPECT_TRUE(material->isKeywordEnabled("FEATURE_B"));
    const auto& rebuilt = shader->defaultSubShader().passes()[0];
    EXPECT_EQ(material->variantKey(rebuilt), 0U);
    material->disableKeyword("UNDECLARED");
    EXPECT_FALSE(material->isKeywordEnabled("UNDECLARED"));
    EXPECT_EQ(rebuilt.programCount(), 0U);
    EXPECT_EQ(shader->clone()->defaultSubShader().passes()[0].keyword().keywords(),
              rebuilt.keyword().keywords());
}

TEST_F(ShaderIsolationTest, ReadsAndRewritesPreSplitV3ArtifactWithoutChangingPayload) {
    // Golden output from the pre-split cooker, not from the current serializer.
    const auto fixture =
        std::filesystem::path{MINI_TEST_SHADER_ISOLATION_FIXTURE_DIR} / "shader_legacy_v3.artifact";
    std::ifstream input{fixture, std::ios::binary};
    ASSERT_TRUE(input);
    const std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
    const auto bytes = std::as_bytes(std::span{raw});
    const auto artifact =
        parseAssetArtifact(VirtualPath{"fixture://shader_legacy_v3.artifact"}, bytes);
    ASSERT_TRUE(artifact);
    ShaderAsset asset;
    asset.setAssetIdentity(artifact->assetId, artifact->sourcePath);
    BinaryReader reader{artifact->payload};
    ASSERT_TRUE(asset.transfer(reader));
    ASSERT_TRUE(reader.finished());
    EXPECT_EQ(asset.data.name, "Tests/LegacyV3");
    ASSERT_EQ(asset.data.properties.size(), 2U);
    EXPECT_EQ(asset.data.properties[0].type, ShaderAssetPropertyType::Range);
    EXPECT_EQ(asset.data.properties[0].displayName, "Legacy Roughness");
    EXPECT_EQ(asset.data.properties[1].type, ShaderAssetPropertyType::Color);
    ASSERT_EQ(asset.data.globalProperties.size(), 1U);
    EXPECT_EQ(asset.data.globalProperties[0].name, "LegacyGlobal");
    BinaryWriter writer;
    ASSERT_TRUE(asset.transfer(writer));
    EXPECT_EQ(writer.bytes(), artifact->payload);
    ASSERT_TRUE(asset.instantiate());
}

} // namespace
