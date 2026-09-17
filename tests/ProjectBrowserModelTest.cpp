#include <gtest/gtest.h>

#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/filesystem/FileSystem.h"
#include "core/filesystem/FileWatcher.h"
#include "tools/editor/ProjectBrowserModel.h"
#include "TestAssetEnvironment.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

namespace {

using namespace engine;
using namespace engine::editor;

constexpr std::string_view kShaderJson = R"json({
  "$schemaVersion": 1,
  "name": "BrowserShader",
  "properties": [
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] }
  ],
  "subShaders": [{
    "passes": [{
      "name": "Forward",
      "lightMode": "Forward",
      "program": { "vertex": "simple.vert", "frag": "simple.frag" }
    }]
  }]
})json";

class ProjectBrowserModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineProjectBrowserModelTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        std::error_code error;
        std::filesystem::create_directories(assets, error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(test::initializeAssetEnvironment(assets));
        FILE_WATCHER.stop();

        ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.vert"},
                                          "#version 450\nvoid main(){gl_Position=vec4(0);}\n"));
        ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.frag"},
                                          "#version 450\nlayout(location=0) out vec4 c;"
                                          "void main(){c=vec4(1);}\n"));
        shaderPath = VirtualPath{"assets://shaders/fixture.shader.json"};
        ASSERT_TRUE(FILE_SYSTEM.writeText(shaderPath, std::string{kShaderJson}));
        ASSERT_TRUE(ASSET_IMPORT_PIPELINE.importAsset(shaderPath));
    }

    void TearDown() override {
        FILE_WATCHER.stop();
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    [[nodiscard]] AssetId metaGuid(const VirtualPath& sourcePath) const {
        const auto meta = loadAssetMeta(assetMetaPath(sourcePath));
        EXPECT_TRUE(meta.has_value()) << sourcePath.string();
        return meta ? meta->assetId : AssetId{};
    }

    [[nodiscard]] static bool
    containsPath(const std::vector<ProjectEntry>& entries, const VirtualPath& path) {
        return std::ranges::any_of(entries,
                                   [&path](const ProjectEntry& entry) { return entry.path == path; });
    }

    std::filesystem::path root;
    std::filesystem::path assets;
    VirtualPath shaderPath;
};

TEST_F(ProjectBrowserModelTest, NavigationTracksHistory) {
    ProjectBrowserModel model;
    EXPECT_EQ(model.currentDirectory().string(), "assets://");
    EXPECT_FALSE(model.canBack());
    EXPECT_FALSE(model.canForward());
    EXPECT_FALSE(model.navigateUp()); // Root caps upward navigation.

    const VirtualPath shaders{"assets://shaders"};
    model.navigate(shaders);
    EXPECT_EQ(model.currentDirectory(), shaders);
    EXPECT_TRUE(model.canBack());
    EXPECT_FALSE(model.canForward());

    EXPECT_TRUE(model.back());
    EXPECT_EQ(model.currentDirectory().string(), "assets://");
    EXPECT_FALSE(model.canBack());
    EXPECT_TRUE(model.canForward());
    EXPECT_TRUE(model.forward());
    EXPECT_EQ(model.currentDirectory(), shaders);

    // Re-navigating the current directory pushes nothing new.
    model.navigate(shaders);
    EXPECT_TRUE(model.back());
    EXPECT_EQ(model.currentDirectory().string(), "assets://");
    EXPECT_TRUE(model.canForward());
    // Missing directories are ignored.
    model.navigate(VirtualPath{"assets://missing"});
    EXPECT_EQ(model.currentDirectory().string(), "assets://");
}

TEST_F(ProjectBrowserModelTest, BreadcrumbsWalkAncestors) {
    ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://a/b/file.txt"}, "x"));
    ProjectBrowserModel model;
    model.navigate(VirtualPath{"assets://a/b"});
    const std::vector<VirtualPath> crumbs = model.breadcrumbs();
    ASSERT_EQ(crumbs.size(), 3);
    EXPECT_EQ(crumbs[0].string(), "assets://");
    EXPECT_EQ(crumbs[1].string(), "assets://a");
    EXPECT_EQ(crumbs[2].string(), "assets://a/b");
}

TEST_F(ProjectBrowserModelTest, ContentListsDirectoriesFirstAndHidesMeta) {
    ProjectBrowserModel model;
    const std::vector<ProjectEntry> entries = model.contentEntries();
    ASSERT_FALSE(entries.empty());
    EXPECT_TRUE(entries.front().directory);
    EXPECT_EQ(entries.front().name, "shaders");

    // The root view only lists direct children; descend to see the file.
    model.navigate(VirtualPath{"assets://shaders"});
    const std::vector<ProjectEntry> files = model.contentEntries();
    EXPECT_TRUE(containsPath(files, shaderPath));
    EXPECT_EQ(std::ranges::count_if(files,
                                    [](const ProjectEntry& entry) {
                                        return entry.name.ends_with(".meta");
                                    }),
              0);
}

TEST_F(ProjectBrowserModelTest, SearchMatchesRecursively) {
    ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://textures/decal.png"}, "png"));
    ProjectBrowserModel model;
    model.navigate(VirtualPath{"assets://shaders"});
    // Search escapes the current directory.
    model.setSearchText("decal");
    std::vector<ProjectEntry> entries = model.contentEntries();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].path.string(), "assets://textures/decal.png");

    model.setSearchText("FIXTURE"); // Case-insensitive.
    entries = model.contentEntries();
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].path, shaderPath);

    model.setSearchText("");
    model.setTypeFilter(AssetType::Shader);
    entries = model.contentEntries(); // Still inside shaders/.
    EXPECT_TRUE(containsPath(entries, shaderPath));
    EXPECT_EQ(std::ranges::count_if(entries,
                                    [](const ProjectEntry& entry) {
                                        return !entry.directory &&
                                               entry.name == "simple.vert";
                                    }),
              0); // Unknown-typed files are filtered out.
    model.setTypeFilter(AssetType::Unknown); // "All" restores them.
    entries = model.contentEntries();
    EXPECT_TRUE(containsPath(entries, VirtualPath{"assets://shaders/simple.vert"}));
}

TEST_F(ProjectBrowserModelTest, CreateFolderCreatesAndValidates) {
    ProjectBrowserModel model;
    EXPECT_TRUE(model.createFolder(VirtualPath{"assets://"}, "Levels"));
    EXPECT_TRUE(FILE_SYSTEM.isDirectory(VirtualPath{"assets://Levels"}));

    EXPECT_FALSE(model.createFolder(VirtualPath{"assets://"}, "Levels"));
    EXPECT_FALSE(model.lastError().empty());

    for (const char* bad : {"", ".", "..", ".hidden", "a/b", "a\\b", "a:b", "x*", "x?",
                            "x\"", "x<", "x>", "x|", "trail ", "trail.", "x.meta"}) {
        EXPECT_FALSE(model.createFolder(VirtualPath{"assets://"}, bad)) << bad;
    }
}

TEST_F(ProjectBrowserModelTest, CreateAssetWritesTemplateAndImports) {
    ProjectBrowserModel model;

    const VirtualPath scenePath{"assets://New Scene.scene.json"};
    ASSERT_TRUE(model.createAsset(VirtualPath{"assets://"}, AssetType::Scene, "New Scene"));
    EXPECT_TRUE(FILE_SYSTEM.isFile(scenePath));
    EXPECT_TRUE(FILE_SYSTEM.isFile(assetMetaPath(scenePath)));
    const auto sceneRecord = ASSET_DATABASE.findByPath(scenePath);
    ASSERT_TRUE(sceneRecord.has_value());
    EXPECT_EQ(sceneRecord->type, AssetType::Scene);
    EXPECT_EQ(sceneRecord->status, AssetImportStatus::Imported);

    const VirtualPath materialPath{"assets://New Material.material.json"};
    ASSERT_TRUE(model.createAsset(VirtualPath{"assets://"}, AssetType::Material, "New Material"));
    const auto materialRecord = ASSET_DATABASE.findByPath(materialPath);
    ASSERT_TRUE(materialRecord.has_value());
    EXPECT_EQ(materialRecord->type, AssetType::Material);
    EXPECT_EQ(materialRecord->status, AssetImportStatus::Imported);

    ASSERT_TRUE(model.createAsset(VirtualPath{"assets://"}, AssetType::Shader, "New Shader"));
    EXPECT_TRUE(
        ASSET_DATABASE.findByPath(VirtualPath{"assets://New Shader.shader.json"}).has_value());

    EXPECT_FALSE(model.createAsset(VirtualPath{"assets://"}, AssetType::Texture, "x"));
    // Duplicate names are rejected up front.
    EXPECT_FALSE(model.createAsset(VirtualPath{"assets://"}, AssetType::Scene, "New Scene"));
}

TEST_F(ProjectBrowserModelTest, RenameKeepsGuidAndUpdatesDatabase) {
    ProjectBrowserModel model;
    const AssetId guid = metaGuid(shaderPath);
    ASSERT_TRUE(guid.valid());

    // Suffix-less input keeps the recognized double suffix.
    ASSERT_TRUE(model.renameEntry(shaderPath, "renamed_fixture"));
    const VirtualPath renamed{"assets://shaders/renamed_fixture.shader.json"};
    EXPECT_TRUE(FILE_SYSTEM.isFile(renamed));
    EXPECT_FALSE(FILE_SYSTEM.isFile(shaderPath));
    EXPECT_FALSE(FILE_SYSTEM.isFile(assetMetaPath(shaderPath)));
    EXPECT_EQ(metaGuid(renamed), guid);
    const auto record = ASSET_DATABASE.findByPath(renamed);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->id, guid);
    EXPECT_EQ(record->sourcePath, renamed);
    EXPECT_EQ(record->status, AssetImportStatus::Imported);
    EXPECT_FALSE(ASSET_DATABASE.findByPath(shaderPath).has_value());

    // Renaming to the current name is a no-op.
    EXPECT_TRUE(model.renameEntry(renamed, "renamed_fixture"));

    // Conflicting destination fails and leaves everything in place.
    ASSERT_TRUE(
        FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/other.shader.json"},
                              std::string{kShaderJson}));
    EXPECT_FALSE(model.renameEntry(renamed, "other.shader.json"));
    EXPECT_TRUE(FILE_SYSTEM.isFile(renamed));
    EXPECT_EQ(metaGuid(renamed), guid);
}

TEST_F(ProjectBrowserModelTest, MoveEntryIntoFolderKeepsGuid) {
    ASSERT_TRUE(FILE_SYSTEM.createDirectories(VirtualPath{"assets://nested"}));
    ProjectBrowserModel model;
    const AssetId guid = metaGuid(shaderPath);

    ASSERT_TRUE(model.moveEntry(shaderPath, VirtualPath{"assets://nested"}));
    const VirtualPath moved{"assets://nested/fixture.shader.json"};
    EXPECT_TRUE(FILE_SYSTEM.isFile(moved));
    EXPECT_FALSE(FILE_SYSTEM.isFile(shaderPath));
    EXPECT_EQ(metaGuid(moved), guid);
    const auto record = ASSET_DATABASE.findByPath(moved);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->id, guid);

    // Moving into the containing directory is a no-op.
    EXPECT_TRUE(model.moveEntry(moved, VirtualPath{"assets://nested"}));
}

TEST_F(ProjectBrowserModelTest, MovingDirectoryUpdatesDescendantRecords) {
    ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://pack/deep/inner.txt"}, "x"));
    ProjectBrowserModel model;
    ASSERT_TRUE(model.moveEntry(shaderPath, VirtualPath{"assets://pack"}));
    const VirtualPath packed{"assets://pack/fixture.shader.json"};
    const AssetId guid = metaGuid(packed);

    // Renaming the directory physically moves the whole tree first, then the
    // model re-points the descendant records (moveEntry targets a directory,
    // so a rename is the right call here).
    ASSERT_TRUE(model.renameEntry(VirtualPath{"assets://pack"}, "pack2"));
    EXPECT_FALSE(FILE_SYSTEM.isDirectory(VirtualPath{"assets://pack"}));
    EXPECT_TRUE(FILE_SYSTEM.isFile(VirtualPath{"assets://pack2/deep/inner.txt"}));

    const VirtualPath moved{"assets://pack2/fixture.shader.json"};
    EXPECT_EQ(metaGuid(moved), guid);
    const auto record = ASSET_DATABASE.findByPath(moved);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->id, guid);
    EXPECT_EQ(record->status, AssetImportStatus::Imported);

    // A directory cannot move into itself or a descendant.
    EXPECT_FALSE(model.moveEntry(VirtualPath{"assets://pack2"}, VirtualPath{"assets://pack2"}));
    EXPECT_FALSE(model.moveEntry(VirtualPath{"assets://pack2"},
                                 VirtualPath{"assets://pack2/deep"}));
}

TEST_F(ProjectBrowserModelTest, RemoveEntryDeletesFileMetaAndRecord) {
    ProjectBrowserModel model;
    model.selectEntry(shaderPath);
    ASSERT_NE(model.selectedEntry(), nullptr);

    ASSERT_TRUE(model.removeEntry(shaderPath));
    EXPECT_FALSE(FILE_SYSTEM.isFile(shaderPath));
    EXPECT_FALSE(FILE_SYSTEM.isFile(assetMetaPath(shaderPath)));
    EXPECT_FALSE(ASSET_DATABASE.findByPath(shaderPath).has_value());

    // The stale selection is pruned when snapshots rebuild.
    (void)model.contentEntries();
    EXPECT_EQ(model.selectedEntry(), nullptr);
}

TEST_F(ProjectBrowserModelTest, RemoveDirectoryRemovesDescendants) {
    ProjectBrowserModel model;
    ASSERT_TRUE(model.removeEntry(VirtualPath{"assets://shaders"}));
    EXPECT_FALSE(FILE_SYSTEM.isDirectory(VirtualPath{"assets://shaders"}));
    EXPECT_FALSE(FILE_SYSTEM.isFile(shaderPath));
    EXPECT_FALSE(ASSET_DATABASE.findByPath(shaderPath).has_value());
    EXPECT_FALSE(model.removeEntry(VirtualPath{"assets://shaders"})); // Already gone.
}

TEST_F(ProjectBrowserModelTest, DuplicateEntryCreatesCopyWithNewGuid) {
    ProjectBrowserModel model;
    const AssetId guid = metaGuid(shaderPath);

    ASSERT_TRUE(model.duplicateEntry(shaderPath));
    const VirtualPath copy{"assets://shaders/fixture 1.shader.json"};
    EXPECT_TRUE(FILE_SYSTEM.isFile(copy));
    EXPECT_NE(metaGuid(copy), guid);
    const auto record = ASSET_DATABASE.findByPath(copy);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->status, AssetImportStatus::Imported);

    // The original keeps its identity.
    EXPECT_EQ(metaGuid(shaderPath), guid);
}

TEST_F(ProjectBrowserModelTest, DuplicateDirectoryRegeneratesGuids) {
    ProjectBrowserModel model;
    const AssetId guid = metaGuid(shaderPath);

    ASSERT_TRUE(model.duplicateEntry(VirtualPath{"assets://shaders"}));
    const VirtualPath copyShader{"assets://shaders 1/fixture.shader.json"};
    EXPECT_TRUE(FILE_SYSTEM.isFile(copyShader));
    EXPECT_TRUE(FILE_SYSTEM.isFile(VirtualPath{"assets://shaders 1/simple.vert"}));
    EXPECT_NE(metaGuid(copyShader), guid);
    EXPECT_TRUE(ASSET_DATABASE.findByPath(copyShader).has_value());
    // The original tree is untouched.
    EXPECT_EQ(metaGuid(shaderPath), guid);
}

TEST_F(ProjectBrowserModelTest, ReimportEntrySucceeds) {
    ProjectBrowserModel model;
    EXPECT_TRUE(model.reimportEntry(shaderPath));
    EXPECT_FALSE(model.reimportEntry(VirtualPath{"assets://shaders"})); // Directory.
    EXPECT_FALSE(model.reimportEntry(VirtualPath{"assets://missing.txt"}));
}

TEST_F(ProjectBrowserModelTest, SnapshotReflectsExternalChangesAfterInvalidate) {
    ProjectBrowserModel model;
    // Build the snapshot before the external write.
    std::vector<ProjectEntry> entries = model.contentEntries();
    EXPECT_TRUE(containsPath(entries, VirtualPath{"assets://shaders"}));

    // Cached: an external write stays invisible until invalidate().
    ASSERT_TRUE(FILE_SYSTEM.writeText(VirtualPath{"assets://external.txt"}, "x"));
    entries = model.contentEntries();
    EXPECT_FALSE(containsPath(entries, VirtualPath{"assets://external.txt"}));
    model.invalidate();
    entries = model.contentEntries();
    EXPECT_TRUE(containsPath(entries, VirtualPath{"assets://external.txt"}));
}

TEST_F(ProjectBrowserModelTest, SelectionIsSingleEntry) {
    ProjectBrowserModel model;
    model.selectEntry(shaderPath);
    ASSERT_NE(model.selectedEntry(), nullptr);
    EXPECT_EQ(*model.selectedEntry(), shaderPath);

    model.selectEntry(VirtualPath{"assets://shaders/simple.vert"});
    EXPECT_EQ(*model.selectedEntry(), VirtualPath{"assets://shaders/simple.vert"});

    model.clearSelection();
    EXPECT_EQ(model.selectedEntry(), nullptr);
}

TEST_F(ProjectBrowserModelTest, ViewModeToggle) {
    ProjectBrowserModel model;
    EXPECT_EQ(model.viewMode(), ProjectBrowserModel::ViewMode::Grid);
    model.setViewMode(ProjectBrowserModel::ViewMode::List);
    EXPECT_EQ(model.viewMode(), ProjectBrowserModel::ViewMode::List);
}

} // namespace
