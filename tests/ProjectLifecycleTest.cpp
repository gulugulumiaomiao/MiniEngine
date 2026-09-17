// Editor-session lifecycle: boot, create a project, open it, exit it, switch to another
// project while the first is open, reopen a session, delete a project and quit the
// editor -- and quit while a project is still open. It also pins the project's pipeline
// cache slot and its copy of the built-in content: a fresh project must own a readable
// shader-cache://pipeline_cache.bin and ship the built-in material/shaders/scene inside
// its own assets/, readable through assets:// (the engine never mounts the built-in
// content as a virtual scheme).
// GPU and window code is out of scope for a headless test, so each stage drives the
// exact pure-logic calls the editor makes (project template, config load, mounts,
// main-scene materialization, registry persistence, cache and content IO) against a
// real temporary directory and fails on any error.
#include "core/filesystem/FileSystem.h"
#include "runtime/config/ProjectConfig.h"
#include "tools/editor/ProjectRegistry.h"
#include "tools/editor/ProjectTemplate.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

using namespace engine;

std::filesystem::path makeTempRoot() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "MiniEngineProjectLifecycleTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

// Mounts exactly what Project open/close touches (Engine::openProject / closeProject
// mount the same four project schemes).
bool mountProject(const std::filesystem::path& root) {
    for (const char* scheme : {"assets", "library", "shader-cache", "shader-bin"})
        (void)FILE_SYSTEM.unmount(scheme);
    return FILE_SYSTEM.mountDirectory("assets", root / "assets", false) &&
           FILE_SYSTEM.mountDirectory("library", root / "library", false) &&
           FILE_SYSTEM.mountDirectory(
               "shader-cache", root / kProjectShaderCacheDirectory, false) &&
           FILE_SYSTEM.mountDirectory(
               "shader-bin", root / kProjectShaderBinaryDirectory, true);
}

// The project pipeline cache as the engine addresses it while the project is open.
const VirtualPath& projectCachePath() {
    static const VirtualPath path{"shader-cache://pipeline_cache.bin"};
    return path;
}

bool hasStandardLayout(const std::filesystem::path& root) {
    for (const std::filesystem::path& subdirectory :
         {root / "assets", root / "library",
          root / kProjectShaderCacheDirectory, root / kProjectShaderBinaryDirectory}) {
        if (!std::filesystem::is_directory(subdirectory))
            return false;
    }
    return true;
}

// Simulates the Vulkan device persisting a cache into the open project: the placeholder
// written by the template disappears (replaced), and the loaded bytes come back through
// the scheme and land in the project directory.
bool checkCacheRoundTrip(const std::filesystem::path& projectRoot,
                         const std::array<std::byte, 4>& seed,
                         int& failCode) {
    const VirtualPath& path = projectCachePath();
    const auto placeholder = FILE_SYSTEM.readBinary(path);
    if (!placeholder)
        return (failCode = 11), false;
    if (!FILE_SYSTEM.writeBinaryAtomic(path, seed))
        return (failCode = 12), false;
    const auto persisted = FILE_SYSTEM.readBinary(path);
    if (!persisted || persisted->size() != seed.size() ||
        !std::equal(seed.begin(), seed.end(), persisted->begin())) {
        return (failCode = 12), false;
    }
    std::error_code error;
    const auto physical = FILE_SYSTEM.resolvePhysicalPath(path);
    const std::filesystem::path onDisk =
        projectRoot / kProjectShaderCacheDirectory / kProjectPipelineCacheFileName;
    if (!physical ||
        !std::filesystem::equivalent(*physical, onDisk, error) || error) {
        return (failCode = 12), false;
    }
    return true;
}

// The built-in content copied at creation must be readable through the project's own
// assets:// scheme, byte-identical to the built-in source on disk, and physically inside
// the project -- the "reads work against the project's assets" guarantee. blinn_phong is
// sample content, so it comes from the samples layer; both layers flatten into assets/,
// which is why the virtual path carries no layer name.
bool checkBuiltinContentReadable(const std::filesystem::path& projectRoot, int& failCode) {
    const VirtualPath assetShader{"assets://shaders/blinn_phong.shader.json"};
    const auto rendered = FILE_SYSTEM.readBinary(assetShader);
    if (!rendered)
        return (failCode = 40), false;
    // The engine never mounts the built-in content; compare against the source file.
    const std::filesystem::path builtinSource{MINI_TEST_BUILTIN_DIR};
    std::ifstream stream{builtinSource / editor::kBuiltinSamplesDirectory / "shaders" /
                             "blinn_phong.shader.json",
                         std::ios::binary};
    if (!stream)
        return (failCode = 40), false;
    std::vector<std::byte> sourceBytes;
    for (std::istreambuf_iterator<char> it{stream}, end; it != end; ++it) {
        sourceBytes.push_back(std::byte{static_cast<unsigned char>(*it)});
    }
    if (sourceBytes != *rendered)
        return (failCode = 40), false;
    std::error_code error;
    const auto physical = FILE_SYSTEM.resolvePhysicalPath(assetShader);
    if (!physical ||
        !std::filesystem::equivalent(*physical,
                                     projectRoot / "assets" / "shaders" /
                                         "blinn_phong.shader.json",
                                     error) ||
        error) {
        return (failCode = 40), false;
    }
    return true;
}

// Exiting a project unmounts its schemes. There is no engine-global content mount to
// keep alive: the built-in content lives inside the project's own assets/.
void unmountProject() {
    for (const char* scheme : {"assets", "library", "shader-cache", "shader-bin"})
        (void)FILE_SYSTEM.unmount(scheme);
}

// The template now copies *.meta sidecars alongside source assets so that built-in GUID
// identities remain stable and cross-asset references resolve on first import.
bool hasMetaFile(const std::filesystem::path& assetsDir) {
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(assetsDir, error), end;
         !error && it != end; ++it) {
        if (it->is_regular_file(error) && !error && it->path().extension() == ".meta")
            return true;
    }
    return false;
}

} // namespace

int main() {
using namespace engine;
using engine::editor::ProjectRegistry;
using engine::editor::createNewProject;

// The merged built-in content ships the demo scene, so freshly created projects have a
// non-empty assets://scenes and selectProjectScene returns this rather than main.scene.
constexpr std::string_view kDemoScenePath =
    "assets://scenes/blinn_phong_showcase.scene.json";

    const std::filesystem::path tempRoot = makeTempRoot();
    // The registry lives in editor.json, persisted through a virtual mount in tests.
    if (!FILE_SYSTEM.mountDirectory("test-life", tempRoot, false))
        return 1;
    const VirtualPath registryPath{"test-life://editor.json"};

    // --- editor boot: no file yet, recent project list is empty ---
    if (ProjectRegistry::load(registryPath))
        return 2;
    ProjectRegistry registry;
    registry.setFilePath(registryPath);
    if (!registry.empty() || !registry.entries().empty())
        return 3;

    // --- create a project from the picker ---
    std::string error;
    const auto createdA = createNewProject(tempRoot, "LifecycleProject", error);
    if (!createdA || !error.empty())
        return 4;
    const std::filesystem::path projectA = *createdA;
    if (!isProjectDirectory(projectA))
        return 5;
    if (!hasStandardLayout(projectA))
        return 6;
    // The project owns its pipeline cache slot from the very first moment.
    const std::filesystem::path cacheFileA =
        projectA / kProjectShaderCacheDirectory / kProjectPipelineCacheFileName;
    if (!std::filesystem::is_regular_file(cacheFileA))
        return 7;
    // And it ships both built-in layers inside its own assets/, so reads never depend on
    // anything outside the project. The layers flatten into one tree: the first three come
    // from core/ (engine contract), the rest from samples/ (demo content).
    for (const std::filesystem::path& copiedFile :
         {projectA / "assets" / "materials" / "error.material.json",
          projectA / "assets" / "shaders" / "builtin_color.shader.json",
          projectA / "assets" / "shaders" / "include" / "scene.glsl",
          projectA / "assets" / "shaders" / "blinn_phong.shader.json",
          projectA / "assets" / "materials" / "builtin_blinn_phong.material.json",
          projectA / "assets" / "scenes" / "default.scene.json"}) {
        if (!std::filesystem::is_regular_file(copiedFile))
            return 8;
    }
    // Meta sidecars ship with the copy so built-in GUID identities are stable.
    if (!hasMetaFile(projectA / "assets"))
        return 42;
    if (!registry.addProject(projectA, "LifecycleProject") || registry.size() != 1)
        return 9;

    // --- open the project (pure-logic half of Engine::openProject) ---
    const std::optional<ProjectConfig> config =
        ProjectConfig::load(projectConfigPath(projectA), error);
    if (!config || config->schemaVersion != 1 || config->name != "LifecycleProject" ||
        config->render.pipeline.empty() || config->window.has_value())
        return 10;
    if (!mountProject(projectA))
        return 11;
    {
        // The engine reads the cache file as soon as the device is created; the seeded
        // placeholder must resolve, read and round-trip back into the project.
        int cacheFail = 0;
        if (!checkCacheRoundTrip(projectA, {std::byte{0x13}, std::byte{0x37}, std::byte{0x2A},
                                             std::byte{0x5C}},
                                 cacheFail))
            return cacheFail;
    }
    {
        int contentFail = 0;
        if (!checkBuiltinContentReadable(projectA, contentFail))
            return contentFail;
    }
    if (!ensureProjectMainScene(error) || !error.empty())
        return 13;
    if (selectProjectScene({}).string() != std::string{kDemoScenePath})
        return 14;
    registry.setLastScene(projectA, std::string{kDemoScenePath});

    // --- exit the project: unmount scheme table, persist editor state ---
    unmountProject();
    if (!registry.save())
        return 15;

    // --- next editor session: the entry survives, the last scene is remembered ---
    auto booted = ProjectRegistry::load(registryPath);
    if (!booted || booted->size() != 1 ||
        booted->lastScene(projectA) != kDemoScenePath)
        return 16;

    // --- create project B, then reopen A ---
    const auto createdB = createNewProject(tempRoot, "SecondProject", error);
    if (!createdB || !error.empty())
        return 17;
    const std::filesystem::path projectB = *createdB;
    if (!hasStandardLayout(projectB))
        return 18;
    if (!std::filesystem::is_regular_file(
            projectB / kProjectShaderCacheDirectory / kProjectPipelineCacheFileName))
        return 19;
    for (const std::filesystem::path& copiedFile :
         {projectB / "assets" / "shaders" / "blinn_phong.shader.json",
          projectB / "assets" / "scenes" / "default.scene.json"}) {
        if (!std::filesystem::is_regular_file(copiedFile))
            return 20;
    }
    if (!hasMetaFile(projectB / "assets"))
        return 43;
    if (!mountProject(projectA))
        return 21;
    if (!ensureProjectMainScene(error))
        return 22;
    if (selectProjectScene({}).string() != std::string{kDemoScenePath})
        return 23;

    // --- switch from project A to project B while A is still open: engine closes A
    //     (unmounts its schemes) and opens B, then the editor records B as recent ---
    unmountProject();
    if (!mountProject(projectB))
        return 24;
    {
        // B's own cache slot is present and readable through the scheme as well.
        int cacheFail = 0;
        if (!checkCacheRoundTrip(projectB, {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                             std::byte{0xEF}},
                                 cacheFail))
            return cacheFail;
    }
    {
        int contentFail = 0;
        if (!checkBuiltinContentReadable(projectB, contentFail))
            return contentFail;
    }
    if (!ensureProjectMainScene(error) || !error.empty())
        return 27;
    if (selectProjectScene({}).string() != std::string{kDemoScenePath})
        return 28;
    // The mounted main scene now lives under B, not A.
    std::error_code resolveError;
    const auto physicalMain =
        FILE_SYSTEM.resolvePhysicalPath(VirtualPath{kDemoScenePath});
    if (!physicalMain ||
        !std::filesystem::equivalent(*physicalMain,
                                     projectB / "assets" / "scenes" /
                                         "blinn_phong_showcase.scene.json",
                                     resolveError) ||
        resolveError)
        return 29;

    if (!booted->addProject(projectB, "SecondProject"))
        return 30;
    booted->setLastScene(projectB, std::string{kDemoScenePath});
    if (booted->size() != 2 || booted->entries()[0].name != "SecondProject" ||
        booted->entries()[1].name != "LifecycleProject")
        return 31;
    // Each project keeps its own last scene across the switch.
    if (booted->lastScene(projectA) != kDemoScenePath)
        return 32;

    // --- exit project B and persist ---
    unmountProject();
    if (!booted->save())
        return 33;

    // --- a later session sees both entries, B first ---
    auto reloaded = ProjectRegistry::load(registryPath);
    if (!reloaded || reloaded->size() != 2 ||
        reloaded->entries()[0].name != "SecondProject")
        return 34;
    if (reloaded->lastScene(projectA) != kDemoScenePath ||
        reloaded->lastScene(projectB) != kDemoScenePath)
        return 35;

    // --- delete project A through the picker (permanent) ---
    if (!reloaded->deleteFromDisk(1, false))
        return 36;
    if (std::filesystem::exists(projectA))
        return 37;
    if (reloaded->size() != 1 || reloaded->entries()[0].name != "SecondProject")
        return 38;

    // --- quit the editor with project B still open: kept on disk, last scene kept ---
    ProjectRegistry quitting = *reloaded;
    quitting.setFilePath(registryPath);
    if (!quitting.save())
        return 39;
    if (!std::filesystem::exists(projectB))
        return 40;
    const auto reopened = ProjectRegistry::load(registryPath);
    if (!reopened || reopened->size() != 1 ||
        reopened->lastScene(projectB) != kDemoScenePath)
        return 41;

    (void)FILE_SYSTEM.unmount("test-life");
    std::error_code cleanupError;
    std::filesystem::remove_all(tempRoot, cleanupError);
    return 0;
}