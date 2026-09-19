#include "asset/format/MaterialAssetFormat.h"
#include "render/material/Material.h"
#include "render/mesh/MeshBuilder.h"
#include "render/renderer/RenderItems.h"
#include "render/scene/RenderScene.h"

#include <gtest/gtest.h>

namespace engine {
namespace {

TEST(RenderDataModelTest, MaterialBatchingFlagsAreMutuallyExclusive) {
    constexpr std::string_view invalid = R"({
        "$schemaVersion": 1,
        "name": "Invalid",
        "shader": "assets://shader.shader.json",
        "static_batch": true,
        "gpu_instancing": true
    })";
    EXPECT_FALSE(format::parseMaterialAsset(VirtualPath{"assets://invalid.material.json"}, invalid));

    constexpr std::string_view valid = R"({
        "$schemaVersion": 1,
        "name": "Instanced",
        "shader": "assets://shader.shader.json",
        "gpu_instancing": true
    })";
    const auto material =
        format::parseMaterialAsset(VirtualPath{"assets://instanced.material.json"}, valid);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(material->batchingMode, MaterialBatchingMode::GpuInstancing);
}

TEST(RenderDataModelTest, ProceduralMeshesAlwaysUseUInt32Indices) {
    MeshBuildRecipe recipe;
    recipe.indexPolicy = MeshIndexPolicy::UInt16; // Legacy input preference is normalized.
    recipe.parts.push_back({PlaneGeometry{}});
    const auto mesh = MeshBuilder::build(recipe);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->desc.indexType, IndexType::UInt32);
    EXPECT_EQ(mesh->data.indices.size(), mesh->data.indexCount * sizeof(std::uint32_t));
}

TEST(RenderDataModelTest, CameraTargetAndDrawStagesHaveExplicitContracts) {
    RenderCamera screenCamera;
    EXPECT_FALSE(screenCamera.target.has_value());
    screenCamera.target = RenderTargetHandle{7, 2};
    ASSERT_TRUE(screenCamera.target.has_value());
    EXPECT_EQ(screenCamera.target->index, 7U);

    SourceDrawGroups groups;
    SourceDrawItem source;
    source.indexRange = {.firstIndex = 3, .indexCount = 12, .vertexOffset = -1};
    groups[2000].push_back(source);
    ASSERT_EQ(groups.at(2000).size(), 1U);
    EXPECT_EQ(groups.at(2000).front().indexRange.indexCount, 12U);

    RenderItem item;
    item.arguments.indexCount = 12;
    item.arguments.instanceCount = 4;
    EXPECT_EQ(item.arguments.instanceCount, 4U);
}

} // namespace
} // namespace engine
