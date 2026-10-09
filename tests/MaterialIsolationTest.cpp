#include "TestAssetEnvironment.h"
#include "TestMaterial.h"
#include "TestRenderDevice.h"
#include "asset/derived_data/AssetArtifact.h"
#include "asset/runtime/MaterialAssetResolver.h"
#include "core/serialization/BinaryTransfer.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/shader/ShaderCompilePipeline.h"
#include "render/shader/ShaderProgram.h"
#include <gtest/gtest.h>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace {
using namespace engine;
static_assert(!std::is_same_v<MaterialAssetData, MaterialCreateInfo>);
static_assert(!std::is_base_of_v<Transferable, Material>);
static_assert(!std::is_base_of_v<Transferable, MaterialCreateInfo>);
static_assert(!std::is_base_of_v<Transferable, TextureDesc>);
static_assert(!std::is_base_of_v<Transferable, SamplerDesc>);

MaterialCreateInfo runtimeInfo() {
    auto info = test::materialInfo({
        {"Value", ShaderPropertyType::Float, 2.0F},
        {"Tint", ShaderPropertyType::Vec4, math::Vec4{1.0F}},
        {"Enabled", ShaderPropertyType::Boolean, true},
        {"Transform", ShaderPropertyType::Matrix, math::Mat44{1.0F}},
        {"FirstTexture", ShaderPropertyType::Texture2D, std::string{}},
        {"SecondTexture", ShaderPropertyType::Texture2D, std::string{}},
    });
    info.properties["Value"] = 7.0F;
    info.properties["Tint"] = math::Vec4{0.1F, 0.2F, 0.3F, 0.4F};
    return info;
}

Ref<ShaderProgram> program(std::vector<ShaderDescriptorBinding> descriptors) {
    static ShaderProgramId nextId = 100;
    CompiledShader vertex;
    CompiledShader fragment;
    fragment.reflection.descriptors = std::move(descriptors);
    return ShaderProgram::create(vertex, fragment, nextId++);
}

template <class T> T read(const std::vector<std::byte>& bytes, std::size_t offset) {
    T value{};
    EXPECT_LE(offset + sizeof(T), bytes.size());
    if (offset + sizeof(T) <= bytes.size())
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

class MaterialGpuTest : public ::testing::Test {
protected:
    void TearDown() override {
        FRAME_GPU_MANAGER.shutdown();
        PipelineLayout::clearCache();
        UniformSetLayout::clearCache();
    }
    MockDevice device;
};

TEST_F(MaterialGpuTest, UploadsReflectedSubsetAtProgramOffsets) {
    auto info = runtimeInfo();
    info.properties["Transform"] = math::Mat44{3.0F};
    auto material = makeRef<Material>(info);
    auto reflected = program({{"Subset",
                               ShaderDescriptorType::UniformBuffer,
                               1,
                               5,
                               {{"Tint", UboUniformType::Vec4, 0, 16},
                                {"Value", UboUniformType::Float, 32, 4},
                                {"Enabled", UboUniformType::Bool, 36, 4},
                                {"Transform", UboUniformType::Mat4, 48, 64}}}});
    ASSERT_TRUE(reflected);
    const auto set = material->uniformSet(*reflected, 0);
    ASSERT_TRUE(set);
    const auto* ubo = set->uboAt(0);
    ASSERT_TRUE(ubo);
    const auto& bytes = device.uploadedBuffers.at(ubo->buffer().index());
    EXPECT_EQ(read<math::Vec4>(bytes, 0), material->getVec4("Tint"));
    EXPECT_FLOAT_EQ(read<float>(bytes, 32), 7.0F);
    EXPECT_EQ(read<std::uint32_t>(bytes, 36), 1U);
    EXPECT_EQ(read<math::Mat44>(bytes, 48), math::Mat44{3.0F});
    EXPECT_EQ(device.descriptorWrites.at(set->descriptorSet().index())[0].binding, 5U);
}

TEST_F(MaterialGpuTest, KeepsSeparateProgramAndFrameBindingsWithCopyOnWrite) {
    auto material = makeRef<Material>(runtimeInfo());
    auto a = program({{"A",
                       ShaderDescriptorType::UniformBuffer,
                       1,
                       0,
                       {{"Value", UboUniformType::Float, 0, 4}}}});
    auto b = program({{"B",
                       ShaderDescriptorType::UniformBuffer,
                       1,
                       0,
                       {{"Tint", UboUniformType::Vec4, 16, 16}}}});
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_EQ(a->setLayout(1), b->setLayout(1));
    const auto first = material->uniformSet(*a, 0);
    const auto otherProgram = material->uniformSet(*b, 0);
    const auto otherFrame = material->uniformSet(*a, 1);
    ASSERT_TRUE(first);
    ASSERT_TRUE(otherProgram);
    ASSERT_TRUE(otherFrame);
    EXPECT_EQ(material->uniformSet(*a, 0), first);
    EXPECT_NE(first, otherProgram);
    EXPECT_NE(first, otherFrame);
    material->setFloat("Value", 9.0F);
    const auto updated = material->uniformSet(*a, 0);
    ASSERT_TRUE(updated);
    EXPECT_NE(first, updated);
    EXPECT_FLOAT_EQ(read<float>(device.uploadedBuffers.at(first->uboAt(0)->buffer().index()), 0),
                    7.0F);
    EXPECT_FLOAT_EQ(read<float>(device.uploadedBuffers.at(updated->uboAt(0)->buffer().index()), 0),
                    9.0F);
    auto clone = material->clone();
    const auto clonedSet = clone->uniformSet(*a, 0);
    EXPECT_NE(updated, clonedSet);
    EXPECT_NE(material->instanceId(), clone->instanceId());
    EXPECT_FLOAT_EQ(clone->getFloat("Value"), 9.0F);
}

TEST_F(MaterialGpuTest, UsesSparseTextureBindingsNamesAndSamplerOverrides) {
    auto material = makeRef<Material>(runtimeInfo());
    const auto white = Texture::defaultWhite();
    const auto black = Texture::defaultBlack();
    const auto point = Sampler::resolve(
        {TextureFilterMode::Point, TextureAddressMode::Repeat, TextureAddressMode::Repeat, 1.0F});
    material->setTexture("FirstTexture", white);
    material->setTexture("SecondTexture", black, point);
    auto first = program({{"FirstTexture", ShaderDescriptorType::CombinedImageSampler, 1, 3, {}}});
    auto second =
        program({{"SecondTexture", ShaderDescriptorType::CombinedImageSampler, 1, 3, {}}});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->setLayout(1), second->setLayout(1));
    const auto set = material->uniformSet(*second, 0);
    ASSERT_TRUE(set);
    const auto& writes = device.descriptorWrites.at(set->descriptorSet().index());
    ASSERT_EQ(writes.size(), 1U);
    EXPECT_EQ(writes[0].binding, 3U);
    EXPECT_EQ(writes[0].textureView, black->defaultView());
    EXPECT_EQ(writes[0].sampler, point->rhiHandle());
    material->setTexture("SecondTexture", white);
    const auto replaced = material->uniformSet(*second, 0);
    ASSERT_TRUE(replaced);
    EXPECT_NE(replaced, set);
    EXPECT_EQ(device.descriptorWrites.at(replaced->descriptorSet().index())[0].textureView,
              white->defaultView());
}

TEST_F(MaterialGpuTest, RejectsIncompatibleMembersAndAllowsNoMaterialSet) {
    auto material = makeRef<Material>(runtimeInfo());
    auto wrongType = program({{"Block",
                               ShaderDescriptorType::UniformBuffer,
                               1,
                               0,
                               {{"Value", UboUniformType::Vec4, 0, 16}}}});
    auto missing = program({{"Block",
                             ShaderDescriptorType::UniformBuffer,
                             1,
                             0,
                             {{"Missing", UboUniformType::Float, 0, 4}}}});
    auto undersized = program({{"Block",
                                ShaderDescriptorType::UniformBuffer,
                                1,
                                0,
                                {{"Tint", UboUniformType::Vec4, 0, 4}}}});
    EXPECT_FALSE(material->uniformSet(*wrongType, 0));
    EXPECT_FALSE(material->uniformSet(*missing, 0));
    EXPECT_FALSE(material->uniformSet(*undersized, 0));
    const auto oversized = program({{"Block",
                                     ShaderDescriptorType::UniformBuffer,
                                     1,
                                     0,
                                     {{"Value", UboUniformType::Float, 0, 16}}}});
    EXPECT_FALSE(material->uniformSet(*oversized, 0));
    const auto count = device.descriptorSets;
    auto none = program({});
    EXPECT_FALSE(material->uniformSet(*none, 0));
    EXPECT_EQ(device.descriptorSets, count);
}

TEST_F(MaterialGpuTest, FrameRetainsBindingsAfterMaterialDestruction) {
    ASSERT_TRUE(FRAME_GPU_MANAGER.initialize());
    FRAME_GPU_MANAGER.beginFrame(0);
    auto reflected = program({{"Block",
                               ShaderDescriptorType::UniformBuffer,
                               1,
                               0,
                               {{"Value", UboUniformType::Float, 0, 4}}}});
    auto material = makeRef<Material>(runtimeInfo());
    auto set = material->uniformSet(*reflected, 0);
    ASSERT_TRUE(set);
    FRAME_GPU_MANAGER.retain(0, set);
    const auto destroyed = device.destroyedDescriptorSets;
    material.reset();
    set.reset();
    EXPECT_EQ(device.destroyedDescriptorSets, destroyed);
    FRAME_GPU_MANAGER.beginFrame(1);
    EXPECT_EQ(device.destroyedDescriptorSets, destroyed);
    FRAME_GPU_MANAGER.beginFrame(0);
    EXPECT_EQ(device.destroyedDescriptorSets, destroyed + 1);
}

TEST(MaterialIsolationTest, LazyShaderRevisionPreservesCompatibleValuesAndKeywords) {
    auto info = runtimeInfo();
    auto material = makeRef<Material>(info);
    material->enableKeyword("UNDECLARED");
    auto replacement = test::materialInfo({
        {"Value", ShaderPropertyType::Float, 0.0F},
        {"Tint", ShaderPropertyType::Float, 0.5F},
        {"NewValue", ShaderPropertyType::Float, 4.0F},
    });
    // Rebuild the same Shader object without a material registry or callback.
    ShaderCreateInfo changed;
    changed.name = "Reloaded";
    changed.properties = replacement.shader->properties();
    ShaderPassCreateInfo pass;
    pass.name = "Forward";
    pass.program = {VirtualPath{"memory://test.vert"}, VirtualPath{"memory://test.frag"}};
    SubShaderCreateInfo sub;
    sub.passes.push_back(pass);
    changed.subShaders.push_back(sub);
    const auto version = material->version();
    ASSERT_TRUE(info.shader->rebuild(changed));
    EXPECT_FLOAT_EQ(material->getFloat("Value"), 7.0F);
    EXPECT_FLOAT_EQ(material->getFloat("Tint"), 0.5F);
    EXPECT_FLOAT_EQ(material->getFloat("NewValue"), 4.0F);
    EXPECT_TRUE(material->isKeywordEnabled("UNDECLARED"));
    EXPECT_EQ(material->version(), version + 1);
    MaterialCreateInfo authoritative;
    authoritative.shader = info.shader;
    ASSERT_TRUE(material->rebuild(authoritative));
    EXPECT_FLOAT_EQ(material->getFloat("Value"), 0.0F);
    EXPECT_TRUE(material->keywords.empty());
    authoritative.properties["Value"] = false;
    const auto stableVersion = material->version();
    EXPECT_FALSE(material->rebuild(authoritative));
    EXPECT_EQ(material->version(), stableVersion);
}

class MaterialAssetIsolationTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR)); }
    void TearDown() override { test::shutdownAssetEnvironment(); }
};

TEST_F(MaterialAssetIsolationTest, ReloadKeepsInstanceResetsRemovedOverridesAndPreservesOnFailure) {
    const VirtualPath path{"assets://materials/warm_vertex_color.material.json"};
    const auto asset = ASSET_MANAGER.loadAsset<MaterialAsset>(path);
    ASSERT_TRUE(asset);
    const auto original = asset->instantiate();
    ASSERT_TRUE(original);
    const auto clone = original->clone();
    const auto id = original->instanceId();
    MaterialAsset updated;
    updated.data = asset->data;
    updated.data.properties.erase("BaseColor");
    BinaryWriter writer;
    ASSERT_TRUE(updated.transfer(writer));
    BinaryReader reader{writer.bytes()};
    ASSERT_TRUE(asset->transfer(reader));
    EXPECT_EQ(asset->instantiate(), original);
    EXPECT_EQ(original->instanceId(), id);
    const auto& properties = original->shader().properties();
    const auto property = std::ranges::find(properties, "BaseColor", &ShaderPropertyInfo::name);
    ASSERT_NE(property, properties.end());
    EXPECT_EQ(original->getVec4("BaseColor"), std::get<math::Vec4>(property->defaultValue));
    EXPECT_NE(clone->getVec4("BaseColor"), original->getVec4("BaseColor"));
    updated.data.properties["BaseColor"] = false;
    BinaryWriter bad;
    ASSERT_TRUE(updated.transfer(bad));
    BinaryReader rejected{bad.bytes()};
    const auto version = original->version();
    EXPECT_FALSE(asset->transfer(rejected));
    EXPECT_EQ(original->version(), version);
    EXPECT_FALSE(asset->data.properties.contains("BaseColor"));
}

TEST_F(MaterialAssetIsolationTest, RuntimeSurvivesAssetReleaseAndErrorMaterialIsStatic) {
    const VirtualPath path{"assets://materials/warm_vertex_color.material.json"};
    auto original = resolveMaterialReference(path);
    ASSERT_TRUE(original);
    const auto color = original->getVec4("BaseColor");
    const auto id = original->instanceId();
    EXPECT_TRUE(findMaterialAsset(*original));
    EXPECT_EQ(resolveMaterialReference(path), original);
    EXPECT_EQ(resolveMaterialReference(VirtualPath{"assets://missing.material.json"}),
              Material::errorMaterial);
    ASSERT_TRUE(Material::errorMaterial);
    ASSET_MANAGER.clear();
    EXPECT_FALSE(findMaterialAsset(*original));
    EXPECT_EQ(original->instanceId(), id);
    EXPECT_EQ(original->getVec4("BaseColor"), color);
    EXPECT_EQ(original->clone()->getVec4("BaseColor"), color);
}

TEST(MaterialIsolationTest, ReadsAndRewritesPreSplitV2ArtifactWithoutChangingPayload) {
    const auto fixture = std::filesystem::path{MINI_TEST_MATERIAL_ISOLATION_FIXTURE_DIR} /
                         "material_legacy_v2.artifact";
    std::ifstream input{fixture, std::ios::binary};
    ASSERT_TRUE(input);
    const std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
    const auto artifact = parseAssetArtifact(VirtualPath{"fixture://material_legacy_v2.artifact"},
                                             std::as_bytes(std::span{raw}));
    ASSERT_TRUE(artifact);
    MaterialAsset asset;
    BinaryReader reader{artifact->payload};
    ASSERT_TRUE(asset.transfer(reader));
    ASSERT_TRUE(reader.finished());
    EXPECT_EQ(asset.data.shader, VirtualPath{"assets://shaders/vertex_color.shader.json"});
    EXPECT_TRUE(asset.data.properties.contains("BaseColor"));
    BinaryWriter writer;
    ASSERT_TRUE(asset.transfer(writer));
    EXPECT_EQ(writer.bytes(), artifact->payload);
    EXPECT_FALSE(asset.runtimeInstance());
}
} // namespace
