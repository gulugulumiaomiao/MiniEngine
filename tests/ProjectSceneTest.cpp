#include "core/filesystem/FileSystem.h"
#include "runtime/config/ProjectConfig.h"
#include "tools/editor/ProjectTemplate.h"
#include "scene/scene/SceneAsset.h"
#include "scene/scene/SceneExport.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace {

std::filesystem::path makeProjectRoot() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "MiniEngineProjectSceneTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "assets" / "scenes", error);
    return root;
}

// Mounts only what the scene helpers need: the project's assets. The engine never mounts
// the built-in content, and no asset manager or GPU device is involved.
bool mountProject(const std::filesystem::path& root) {
    (void)FILE_SYSTEM.unmount("assets");
    return FILE_SYSTEM.mountDirectory("assets", root / "assets", false);
}

// copy_file cannot replace a file that is already there on this toolchain when the two
// paths share a volume, and overwrite_existing does not change that, so clear the
// destination first. See copyBuiltinLayer in ProjectTemplate.cpp.
bool copyBinaryFile(const std::filesystem::path& source, const std::filesystem::path& dest) {
    std::error_code error;
    std::filesystem::remove(dest, error);
    if (error)
        return false;
    std::filesystem::copy_file(source, dest, error);
    return !error;
}

} // namespace

int main() {
    using namespace engine;

    const std::filesystem::path root = makeProjectRoot();
    if (!mountProject(root))
        return 1;

    // The built-in default Scene lives inside the project's own assets/scenes/ (that is
    // what seedSampleContentIntoProject copies), referenced like any user asset.
    const VirtualPath defaultScene{kBuiltinDefaultScenePath};
    const VirtualPath mainScene{kProjectMainScenePath};

    // --- an empty project has no scene to open yet ---
    if (selectProjectScene({}).valid())
        return 2;

    // --- the project owns the default Scene bytes; no engine mount is involved ---
    const std::filesystem::path builtinSource{MINI_TEST_BUILTIN_DIR};
    if (!copyBinaryFile(builtinSource / editor::kBuiltinSamplesDirectory / "scenes" /
                            "default.scene.json",
                        root / "assets" / "scenes" / "default.scene.json"))
        return 3;
    if (!mountProject(root))
        return 4;
    if (!FILE_SYSTEM.isFile(defaultScene))
        return 5;
    const std::optional<std::string> defaultContents = FILE_SYSTEM.readText(defaultScene);
    if (!defaultContents)
        return 6;
    const std::shared_ptr<SceneAsset> parsed =
        detail::parseSceneAsset(defaultScene, *defaultContents);
    if (!parsed || parsed->name != "Scene" || parsed->nodes.size() != 3)
        return 7;
    // Verify the default scene contains camera, light, and plane.
    bool hasCamera = false, hasLight = false, hasPlane = false;
    for (const SceneNodeAsset& node : parsed->nodes) {
        if (node.name == "Main Camera") hasCamera = true;
        if (node.name == "Directional Light") hasLight = true;
        if (node.name == "Ground Plane") hasPlane = true;
    }
    if (!hasCamera || !hasLight || !hasPlane)
        return 7;
    if (!validateSceneAsset(*parsed, defaultScene))
        return 8;

    // --- selection resolves to the first scene in the project ---
    if (selectProjectScene({}) != defaultScene)
        return 9;

    // --- ensureProjectMainScene is a no-op while scenes exist: it never fabricates a
    //     main.scene.json on top of the shipped content and never modifies it ---
    std::string error;
    if (!ensureProjectMainScene(error) || !error.empty())
        return 10;
    if (FILE_SYSTEM.isFile(mainScene))
        return 11;
    if (FILE_SYSTEM.readText(defaultScene) != defaultContents)
        return 12;
    if (!ensureProjectMainScene(error))
        return 13;
    if (FILE_SYSTEM.readText(defaultScene) != defaultContents)
        return 14;

    // --- an explicitly preferred scene wins when it exists ---
    const VirtualPath other{"assets://scenes/other.scene.json"};
    SceneAsset edited;
    edited.name = "Edited";
    edited.nodes.push_back(
        SceneNodeAsset{1, std::nullopt, "Node", true, {TransformComponentAsset{}}});
    if (!FILE_SYSTEM.writeTextAtomic(other, writeSceneAssetJson(edited)))
        return 15;
    if (selectProjectScene(other) != other)
        return 16;
    // Missing, wrong scheme and non-scene preferences all fall back to the first scene.
    if (selectProjectScene(VirtualPath{"assets://scenes/absent.scene.json"}) != defaultScene ||
        selectProjectScene(VirtualPath{"builtin://scenes/default.scene.json"}) != defaultScene ||
        selectProjectScene(VirtualPath{"assets://scenes/other.txt"}) != defaultScene ||
        selectProjectScene({}) != defaultScene) {
        return 17;
    }

    // --- without the default scene the next scene by name is used ---
    const auto physicalDefault = FILE_SYSTEM.resolvePhysicalPath(defaultScene);
    if (!physicalDefault)
        return 18;
    std::error_code removeError;
    std::filesystem::remove(*physicalDefault, removeError);
    if (removeError || FILE_SYSTEM.isFile(defaultScene))
        return 19;
    if (selectProjectScene({}) != other)
        return 20;

    // --- a project without its built-in default Scene cannot fabricate one, and fails
    //     honestly instead of inventing content ---
    const auto physicalOther = FILE_SYSTEM.resolvePhysicalPath(other);
    if (!physicalOther)
        return 21;
    std::filesystem::remove(*physicalOther, removeError);
    if (removeError || FILE_SYSTEM.isFile(other))
        return 22;
    if (selectProjectScene({}).valid())
        return 23;
    if (ensureProjectMainScene(error) || error.empty())
        return 24;

    (void)FILE_SYSTEM.unmount("assets");
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    return 0;
}