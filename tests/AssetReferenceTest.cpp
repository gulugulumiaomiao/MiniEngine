#include <gtest/gtest.h>

#include "asset/base/AssetReference.h"
#include "asset/base/AssetId.h"
#include "asset/base/GuidResolver.h"
#include "core/filesystem/VirtualPath.h"

#include <unordered_map>

namespace engine {

class MockGuidResolver final : public GuidResolver {
public:
    std::unordered_map<AssetId, VirtualPath, std::hash<AssetId>> guidToPath;
    std::unordered_map<std::string, AssetId> pathToGuid;

    std::optional<VirtualPath> findPath(const AssetId& guid) const override {
        const auto found = guidToPath.find(guid);
        return found == guidToPath.end() ? std::nullopt
                                         : std::optional<VirtualPath>{found->second};
    }

    std::optional<AssetId> findGuid(const VirtualPath& path) const override {
        const auto found = pathToGuid.find(path.string());
        return found == pathToGuid.end() ? std::nullopt : std::optional<AssetId>{found->second};
    }
};

TEST(AssetReferenceTest, ParsesGuidString) {
    const AssetId id = AssetId::generate();
    const std::string text = std::string{"guid://"} + id.toString();
    const AssetReference ref{text};
    EXPECT_TRUE(ref.valid());
    EXPECT_TRUE(ref.isGuid());
    EXPECT_FALSE(ref.isPath());
    EXPECT_EQ(ref.guid(), id);
    EXPECT_EQ(ref.toString(), text);
}

TEST(AssetReferenceTest, ParsesPathString) {
    const AssetReference ref{"assets://materials/ground.material.json"};
    EXPECT_TRUE(ref.valid());
    EXPECT_FALSE(ref.isGuid());
    EXPECT_TRUE(ref.isPath());
    EXPECT_EQ(ref.path(), VirtualPath{"assets://materials/ground.material.json"});
    EXPECT_EQ(ref.toString(), "assets://materials/ground.material.json");
}

TEST(AssetReferenceTest, RejectsInvalidGuidPrefix) {
    const AssetReference ref{"guid://not-a-guid"};
    EXPECT_FALSE(ref.valid());
}

TEST(AssetReferenceTest, RejectsEmptyString) {
    const AssetReference ref{""};
    EXPECT_FALSE(ref.valid());
}

TEST(AssetReferenceTest, ConstructFromGuidOrPath) {
    const AssetId id = AssetId::generate();
    const AssetReference fromGuid{AssetReference{id}};
    EXPECT_TRUE(fromGuid.isGuid());
    EXPECT_EQ(fromGuid.guid(), id);

    const AssetReference fromPath{AssetReference{VirtualPath{"assets://a/b.json"}}};
    EXPECT_TRUE(fromPath.isPath());
    EXPECT_EQ(fromPath.path(), VirtualPath{"assets://a/b.json"});
}

TEST(GuidResolverTest, ResolvesBothDirections) {
    MockGuidResolver resolver;
    const AssetId id = AssetId::generate();
    const VirtualPath path{"assets://shaders/color.shader.json"};
    resolver.guidToPath[id] = path;
    resolver.pathToGuid[path.string()] = id;

    EXPECT_EQ(resolver.findPath(id), path);
    EXPECT_EQ(resolver.findGuid(path), id);
    EXPECT_EQ(resolver.findPath(AssetId::generate()), std::nullopt);
    EXPECT_EQ(resolver.findGuid(VirtualPath{"assets://missing.json"}), std::nullopt);
}

} // namespace engine
