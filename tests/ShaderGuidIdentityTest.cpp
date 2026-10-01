#include "render/shader/Shader.h"
#include "asset/types/ShaderAsset.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

constexpr const char* kBuiltinColorPath = "assets://shaders/builtin_color.shader.json";

// Shader 已去掉 ShaderResourceManager，改为与 TextureAsset/MeshAsset 同构的“无 Manager”模型：
// resolveShaderReference 经 AssetManager 加载 ShaderAsset 并 instantiate，“每 asset 唯一实例”
// 天然承担去重；clone 产出脱离 asset 的独立实例；热重载经 reloadInPlace→transfer→syncInstance。
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
    EXPECT_TRUE(first->isAssetBacked());
    EXPECT_EQ(first->assetPath(), VirtualPath{kBuiltinColorPath});

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
    EXPECT_TRUE(original->isAssetBacked());
    EXPECT_FALSE(cloned->isAssetBacked());
    // 新模型：clone 脱离 asset，不再保留 assetPath（与 TextureAsset::clone 一致）。
    EXPECT_FALSE(cloned->assetPath().valid());
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

    // 就地重传：缓存的 ShaderAsset 重新 transfer → syncInstance → rebuildFromAsset（revision++）。
    ASSET_MANAGER.reloadInPlace(path);

    EXPECT_GT(original->revision(), originalRevision);
    EXPECT_EQ(cloned->revision(), clonedRevision); // 脱离实例不受 asset 重载影响
}

} // namespace
