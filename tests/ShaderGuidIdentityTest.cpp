#include "asset/runtime/ShaderAssetResolver.h"
#include "render/shader/Shader.h"
#include "asset/types/ShaderAsset.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

constexpr const char* kBuiltinColorPath = "assets://shaders/builtin_color.shader.json";

// Asset-side resolution shares a runtime instance; clones hold independent snapshots.
class ShaderGuidIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine::test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR));
    }

    void TearDown() override { engine::test::shutdownAssetEnvironment(); }
};

TEST_F(ShaderGuidIdentityTest, ResolveSameReferenceReturnsSameInstance) {
    using namespace engine;
    const Ref<Shader> first = resolveShaderReference(kBuiltinColorPath);
    ASSERT_TRUE(first);
    EXPECT_TRUE(static_cast<bool>(findShaderAsset(*first)));
    EXPECT_EQ(shaderAssetPath(*first), VirtualPath{kBuiltinColorPath});

    // 同一引用反复解析命中同一唯一实例（按 asset 去重）。
    const Ref<Shader> second = resolveShaderReference(kBuiltinColorPath);
    EXPECT_EQ(second, first);

    // 直接经 asset instantiate 也命中同一唯一实例。
    const Ref<ShaderAsset> asset =
        ASSET_MANAGER.loadAsset<ShaderAsset>(VirtualPath{kBuiltinColorPath});
    ASSERT_TRUE(asset);
    EXPECT_EQ(asset->instantiate(), first);
}

TEST_F(ShaderGuidIdentityTest, CloneReturnsDifferentInstanceAndIsNotAssetBacked) {
    using namespace engine;
    const Ref<Shader> original = resolveShaderReference(kBuiltinColorPath);
    ASSERT_TRUE(original);

    const Ref<Shader> cloned = original->clone();
    ASSERT_TRUE(cloned);
    EXPECT_NE(cloned, original);
    EXPECT_TRUE(static_cast<bool>(findShaderAsset(*original)));
    EXPECT_FALSE(static_cast<bool>(findShaderAsset(*cloned)));
    // 新模型：clone 脱离 asset，不再保留 assetPath（与 TextureAsset::clone 一致）。
    EXPECT_FALSE(shaderAssetPath(*cloned).valid());
    // 内容仍是 asset 的快照副本。
    EXPECT_EQ(cloned->name(), original->name());
}

TEST_F(ShaderGuidIdentityTest, CloneIsNotTheResolvedInstance) {
    using namespace engine;
    const Ref<Shader> original = resolveShaderReference(kBuiltinColorPath);
    ASSERT_TRUE(original);
    const Ref<Shader> cloned = original->clone();
    ASSERT_TRUE(cloned);

    // 再次解析仍返回唯一实例，而非脱离的 clone。
    const Ref<Shader> resolvedAgain = resolveShaderReference(kBuiltinColorPath);
    EXPECT_EQ(resolvedAgain, original);
    EXPECT_NE(resolvedAgain, cloned);
}

TEST_F(ShaderGuidIdentityTest, ReloadInPlaceUpdatesInstanceButNotClone) {
    using namespace engine;
    const VirtualPath path{kBuiltinColorPath};
    const Ref<Shader> original = resolveShaderReference(path.string());
    ASSERT_TRUE(original);
    const Ref<Shader> cloned = original->clone();
    ASSERT_TRUE(cloned);

    const std::uint64_t originalRevision = original->revision();
    const std::uint64_t clonedRevision = cloned->revision();

    // Asset reload pushes converted runtime data into the existing instance (revision++).
    ASSET_MANAGER.reloadInPlace(path);

    EXPECT_GT(original->revision(), originalRevision);
    EXPECT_EQ(cloned->revision(), clonedRevision); // 脱离实例不受 asset 重载影响
}

} // namespace
