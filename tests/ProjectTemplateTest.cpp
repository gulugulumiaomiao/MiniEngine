#include "core/filesystem/FileSystem.h"
#include "runtime/config/ProjectConfig.h"
#include "tools/editor/ProjectTemplate.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace {

std::filesystem::path makeTemporaryParent() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "MiniEngineProjectTemplateTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

bool writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream)
        return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        return {};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// Verifies the ownership split between the two built-in layers against one project:
// contract assets are engine-owned and come back after being deleted, sample content is
// seeded once and then belongs to the project, so a local edit must survive both a
// re-sync and a re-seed. Returns 0 when the contract holds, otherwise offset plus the
// number of the assertion that failed; the offset tells the rounds apart.
int checkLayerOwnership(const std::filesystem::path& root, int offset) {
    const std::filesystem::path sampleMaterial =
        root / "assets" / "materials" / "blinn_gold.material.json";
    const std::filesystem::path contractMaterial =
        root / "assets" / "materials" / "error.material.json";
    const std::filesystem::path contractInclude =
        root / "assets" / "shaders" / "include" / "scene.glsl";
    if (!std::filesystem::is_regular_file(sampleMaterial) ||
        !std::filesystem::is_regular_file(contractMaterial) ||
        !std::filesystem::is_regular_file(contractInclude))
        return offset + 30;

    const std::string editedMarker = "{\"$schemaVersion\": 1, \"edited\": true}";
    if (!writeTextFile(sampleMaterial, editedMarker))
        return offset + 31;
    std::error_code removeError;
    std::filesystem::remove(contractMaterial, removeError);
    std::filesystem::remove(contractInclude, removeError);
    if (removeError)
        return offset + 32;

    // This is what every project open runs.
    std::string layerError;
    if (!engine::editor::syncEngineContractIntoProject(root, layerError) || !layerError.empty())
        return offset + 33;
    // It restores what the engine cannot run without ...
    if (!std::filesystem::is_regular_file(contractMaterial) ||
        !std::filesystem::is_regular_file(contractInclude))
        return offset + 34;
    // ... and leaves the user's edit alone, because that file is not in the layer.
    if (readTextFile(sampleMaterial) != editedMarker)
        return offset + 35;

    // Re-seeding fills gaps and never overwrites, so an edited sample file stays edited.
    if (!engine::editor::seedSampleContentIntoProject(root, layerError) || !layerError.empty())
        return offset + 36;
    if (readTextFile(sampleMaterial) != editedMarker)
        return offset + 37;
    // A deleted sample file does come back, which is what makes seeding idempotent.
    const std::filesystem::path sampleScene =
        root / "assets" / "scenes" / "blinn_phong_showcase.scene.json";
    std::filesystem::remove(sampleScene, removeError);
    if (removeError)
        return offset + 38;
    if (!engine::editor::seedSampleContentIntoProject(root, layerError) || !layerError.empty())
        return offset + 39;
    if (!std::filesystem::is_regular_file(sampleScene))
        return offset + 40;
    return 0;
}

} // namespace

int main() {
    using namespace engine;
    using engine::editor::createNewProject;

    const std::filesystem::path parent = makeTemporaryParent();
    // createNewProject copies both built-in layers (core/ and samples/) from the source
    // tree's builtin/ directory into assets/, flattened into one tree, with no virtual
    // mount involved.

    // --- invalid names are rejected before any directory is created ---
    std::string error;
    if (createNewProject(parent, "", error))
        return 2;
    if (createNewProject(parent, "bad/name", error))
        return 3;
    if (createNewProject(parent, "bad:name", error))
        return 4;
    if (std::filesystem::exists(parent / "bad:name"))
        return 5;

    // --- a valid creation produces the full directory layout ---
    const std::optional<std::filesystem::path> root =
        createNewProject(parent, "TestProject", error);
    if (!root || root->empty())
        return 6;
    if (!std::filesystem::is_directory(*root / "assets"))
        return 7;
    if (!std::filesystem::is_directory(*root / "library"))
        return 8;
    if (!std::filesystem::is_directory(*root / "generated-shaders/runtime"))
        return 9;
    if (!std::filesystem::is_directory(*root / "generated-shaders/compiled"))
        return 10;

    // --- the project owns a copy of the built-in content in its own assets ---
    if (!std::filesystem::is_regular_file(*root / "assets" / "shaders" /
                                          "blinn_phong.shader.json"))
        return 11;
    if (!std::filesystem::is_regular_file(*root / "assets" / "materials" /
                                          "builtin_blinn_phong.material.json"))
        return 12;
    if (!std::filesystem::is_regular_file(*root / "assets" / "scenes" /
                                          "default.scene.json"))
        return 13;
    if (!std::filesystem::is_regular_file(*root / "assets" / "meshes" /
                                          "procedural_showcase.mesh.json"))
        return 22;
    if (!std::filesystem::is_regular_file(*root / "assets" / "textures" / "checker.png"))
        return 23;

    // Companion Meta files are never copied: they carry source identity placeholders
    // and would be rejected by the project's importer; the project generates its own.
    for (std::filesystem::recursive_directory_iterator it(*root / "assets"), end; it != end;
         ++it) {
        if (it->is_regular_file() && it->path().extension() == ".meta")
            return 14;
    }

    // --- project.json can be loaded back and carries the right name ---
    const std::optional<ProjectConfig> loaded =
        ProjectConfig::load(projectConfigPath(*root), error);
    if (!loaded || loaded->name != "TestProject")
        return 15;
    if (!isProjectDirectory(*root))
        return 16;

    // --- the copied built-in content includes the demo scene ---
    if (!std::filesystem::is_regular_file(*root / "assets" / "scenes" /
                                          "blinn_phong_showcase.scene.json"))
        return 17;
    // --- creation never generates a main.scene.json; it materializes on first open
    //     only while the project has no scenes yet ---
    if (std::filesystem::exists(*root / "assets" / "scenes" / "main.scene.json"))
        return 21;

    // --- the two layers differ in who owns the content once the project exists ---
    // Contract assets are engine-owned and repaired on every open; sample content is
    // seeded once and then belongs to the project, so local edits must survive.
    if (const int code = checkLayerOwnership(*root, 0))
        return code;

    // --- duplicate creation is rejected ---
    if (createNewProject(parent, "TestProject", error))
        return 18;
    if (error.empty())
        return 19;
    if (!std::filesystem::exists(parent / "TestProject"))
        return 20;

    // --- the same contract again, from a project on the built-in content's own volume ---
    // copy_file only refuses to replace an existing file when source and destination sit
    // on one volume, so every check above can pass from a temp directory while a project
    // created next to the engine source tree fails on the second sync. The build tree and
    // MINI_TEST_BUILTIN_DIR are both under one checkout and therefore on one volume.
    // Skipped for a checkout that builds onto another drive, which the round above covers.
    const std::filesystem::path scratchParent{MINI_TEST_SCRATCH_DIR};
    const bool sameVolume =
        scratchParent.root_name() == std::filesystem::path{MINI_TEST_BUILTIN_DIR}.root_name();
    if (sameVolume) {
        std::error_code scratchError;
        std::filesystem::remove_all(scratchParent, scratchError);
        std::filesystem::create_directories(scratchParent, scratchError);
        if (scratchError)
            return 41;
        const std::optional<std::filesystem::path> volumeRoot =
            createNewProject(scratchParent, "SameVolumeProject", error);
        if (!volumeRoot)
            return 42;
        if (const int code = checkLayerOwnership(*volumeRoot, 100))
            return code;
        std::filesystem::remove_all(scratchParent, scratchError);
    }

    // --- cleanup ---
    std::error_code cleanupError;
    std::filesystem::remove_all(parent, cleanupError);
    return 0;
}