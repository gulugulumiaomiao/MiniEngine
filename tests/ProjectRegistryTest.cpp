#include "core/filesystem/FileSystem.h"
#include "runtime/config/ProjectConfig.h"
#include "tools/editor/ProjectRegistry.h"
#include "tools/editor/ProjectTemplate.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace {

std::filesystem::path makeTemporaryRoot() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "MiniEngineProjectRegistryTest";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

std::filesystem::path createFakeProject(const std::filesystem::path& parent,
                                        const std::string& name) {
    std::string error;
    const auto result = engine::editor::createNewProject(parent, name, error);
    return result.value_or(std::filesystem::path{});
}

} // namespace

int main() {
    using namespace engine;
    using engine::editor::ProjectRegistry;

    const std::filesystem::path tempRoot = makeTemporaryRoot();
    // Mount the temp directory so the registry can be read/written through the
    // virtual filesystem (the registry's standalone load/save utility).
    if (!FILE_SYSTEM.mountDirectory("test-registry", tempRoot, false))
        return 1;

    const VirtualPath registryPath{"test-registry://editor.json"};

    // --- empty registry ---
    if (ProjectRegistry::load(registryPath))
        return 2;

    ProjectRegistry registry;
    registry.setFilePath(registryPath);
    if (!registry.empty())
        return 3;

    // --- add and deduplicate ---
    const std::filesystem::path projectA = createFakeProject(tempRoot, "Alpha");
    const std::filesystem::path projectB = createFakeProject(tempRoot, "Bravo");
    if (projectA.empty() || projectB.empty())
        return 4;

    if (!registry.addProject(projectA, "Alpha"))
        return 5;
    if (!registry.addProject(projectB, "Bravo"))
        return 6;
    if (registry.size() != 2)
        return 7;
    if (registry.entries()[0].name != "Bravo")
        return 8;

    if (!registry.addProject(projectA, "AlphaRenamed"))
        return 9;
    if (registry.size() != 2)
        return 10;
    if (registry.entries()[0].name != "AlphaRenamed")
        return 11;

    // --- capacity limit ---
    for (std::uint32_t i = 0; i < ProjectRegistry::kMaxEntries - 2; ++i) {
        const std::string name = "Extra" + std::to_string(i);
        const std::filesystem::path extra = createFakeProject(tempRoot, name);
        if (extra.empty())
            return 12;
        if (!registry.addProject(extra, name))
            return 13;
    }
    if (registry.size() != ProjectRegistry::kMaxEntries)
        return 14;

    const std::filesystem::path overflow = createFakeProject(tempRoot, "Overflow");
    if (registry.addProject(overflow, "Overflow"))
        return 15;

    // --- last_scene round trip ---
    registry.setLastScene(projectA, "assets://scenes/test.scene.json");
    if (registry.lastScene(projectA) != "assets://scenes/test.scene.json")
        return 16;
    registry.setLastScene(projectA, "");
    if (!registry.lastScene(projectA).empty())
        return 17;

    // --- save and load via virtual path ---
    registry.setLastScene(projectB, "assets://scenes/bravo.scene.json");
    if (!registry.save())
        return 18;

    const auto loaded = ProjectRegistry::load(registryPath);
    if (!loaded)
        return 19;
    if (loaded->size() != registry.size())
        return 20;
    if (loaded->lastScene(projectB) != "assets://scenes/bravo.scene.json")
        return 21;

    // --- removeEntry ---
    ProjectRegistry mutableRegistry = *loaded;
    mutableRegistry.setFilePath(registryPath);
    const std::size_t beforeSize = mutableRegistry.size();
    mutableRegistry.removeEntry(0);
    if (mutableRegistry.size() != beforeSize - 1)
        return 22;

    // --- moveToTop ---
    if (mutableRegistry.size() >= 2) {
        const std::string firstName = mutableRegistry.entries()[0].name;
        mutableRegistry.moveToTop(mutableRegistry.size() - 1);
        if (mutableRegistry.entries()[0].name == firstName && mutableRegistry.size() > 2)
            return 23;
    }

    // --- pruneInvalid ---
    std::error_code removeError;
    std::filesystem::remove_all(projectA, removeError);
    if (removeError || std::filesystem::exists(projectA))
        return 24;
    mutableRegistry.pruneInvalid();
    for (const editor::RecentProjectEntry& entry : mutableRegistry.entries()) {
        if (entry.rootDirectory == projectA)
            return 25;
    }

    mutableRegistry.removeEntry(9999);

    (void)FILE_SYSTEM.unmount("test-registry");
    std::error_code cleanupError;
    std::filesystem::remove_all(tempRoot, cleanupError);
    return 0;
}
