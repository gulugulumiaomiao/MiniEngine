#include "tools/editor/ProjectTemplate.h"

#if defined(MINI_EDITOR)

#include "core/logging/Log.h"
#include "runtime/config/ProjectConfig.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>

namespace engine::editor {
namespace {

// How copyBuiltinLayer treats a file the project already has.
enum class BuiltinCollision { Overwrite, Skip };

// Both built-in layers flatten into projectRoot/assets, so only the source subtree and
// the collision policy differ. The engine never mounts the built-in content as a virtual
// scheme: the editor is compiled with the source tree's builtin/ directory baked in, so a
// project reads and ships independently of the engine's location. Companion *.meta files
// are copied so that built-in asset GUIDs are stable and cross-asset references resolve
// correctly on first import.
//
// The collision policy is applied here rather than through copy_options, because on this
// toolchain copy_file cannot replace a file that is already there: with the destination
// present it fails with "File exists" for every copy_options value and leaves the
// destination untouched, even though Win32 CopyFileW with bFailIfExists=FALSE overwrites
// the same pair without complaint. It only happens when source and destination share a
// volume, so a project on a different drive than the engine's source tree never hit it --
// and neither did a test that built its project under temp_directory_path(). Deciding
// existence here works on both layouts: an overwrite removes the destination first and
// copies into the gap, a skip leaves the project's own file alone.
bool copyBuiltinLayer(const std::filesystem::path& projectRoot,
                      std::string_view layer,
                      BuiltinCollision collision,
                      std::string& error) {
    const std::filesystem::path layerRoot =
        std::filesystem::path{MINI_SOURCE_BUILTIN_DIR} / layer;
    std::error_code directoryError;
    if (!std::filesystem::is_directory(layerRoot, directoryError) || directoryError) {
        error = "Cannot find the engine built-in content directory: " + layerRoot.string();
        return false;
    }
    const std::filesystem::path assetsTarget = projectRoot / kProjectAssetsDirectory;
    for (std::filesystem::recursive_directory_iterator layerIterator(layerRoot,
                                                                     directoryError),
         end;
         !directoryError && layerIterator != end; ++layerIterator) {
        const std::filesystem::path relative =
            std::filesystem::relative(layerIterator->path(), layerRoot, directoryError);
        if (directoryError)
            break;
        const std::filesystem::path destination = assetsTarget / relative;
        if (layerIterator->is_directory(directoryError)) {
            std::filesystem::create_directories(destination, directoryError);
        } else if (layerIterator->is_regular_file(directoryError)) {
            std::filesystem::create_directories(destination.parent_path(), directoryError);
            if (directoryError)
                break;
            std::error_code existsError;
            if (std::filesystem::exists(destination, existsError)) {
                if (collision == BuiltinCollision::Skip)
                    continue;
                std::filesystem::remove(destination, directoryError);
                if (directoryError)
                    break;
            } else if (existsError) {
                directoryError = existsError;
                break;
            }
            std::filesystem::copy_file(layerIterator->path(), destination, directoryError);
        }
        if (directoryError)
            break;
    }
    if (directoryError) {
        error = "Cannot copy the built-in content into the project assets: " +
                std::string{directoryError.message()};
        return false;
    }
    return true;
}

} // namespace

bool syncEngineContractIntoProject(const std::filesystem::path& projectRoot,
                                   std::string& error) {
    return copyBuiltinLayer(projectRoot, kBuiltinCoreDirectory, BuiltinCollision::Overwrite, error);
}

bool seedSampleContentIntoProject(const std::filesystem::path& projectRoot,
                                  std::string& error) {
    return copyBuiltinLayer(projectRoot, kBuiltinSamplesDirectory, BuiltinCollision::Skip, error);
}

std::optional<std::filesystem::path>
createNewProject(const std::filesystem::path& parentDirectory, const std::string& name,
                 std::string& error) {
    ProjectConfig probe;
    probe.name = name;
    if (!probe.validate(error))
        return std::nullopt;

    const std::filesystem::path root =
        std::filesystem::absolute(parentDirectory).lexically_normal() / name;
    if (std::filesystem::exists(root)) {
        error = "A directory with this name already exists: " + root.string();
        return std::nullopt;
    }

    const std::array<std::string_view, 4> subdirectories = {
        kProjectAssetsDirectory, kProjectLibraryDirectory,
        kProjectShaderCacheDirectory, kProjectShaderBinaryDirectory};

    std::error_code directoryError;
    for (const std::string_view& subdirectory : subdirectories) {
        std::filesystem::create_directories(root / subdirectory, directoryError);
        if (directoryError) {
            error = "Cannot create directory: " + (root / std::filesystem::path{subdirectory}).string();
            std::filesystem::remove_all(root, directoryError);
            return std::nullopt;
        }
    }

    // The project ships the engine's built-in content inside its own assets/, so opening
    // it never depends on an engine-side mount: the scene's relative references into
    // materials/ and shaders/ keep resolving from the project's assets. The contract layer
    // goes first because the sample content references it (the demo materials use the
    // shared GLSL includes), and seeding must not overwrite what the contract just wrote.
    if (!syncEngineContractIntoProject(root, error) ||
        !seedSampleContentIntoProject(root, error)) {
        std::filesystem::remove_all(root, directoryError);
        return std::nullopt;
    }

    ProjectConfig config;
    config.name = name;
    if (!config.save(projectConfigPath(root), error)) {
        std::filesystem::remove_all(root, directoryError);
        return std::nullopt;
    }

    // Seed the pipeline cache slot so the project owns the file from the start: the
    // engine reads shader-cache://pipeline_cache.bin when the project opens, and a
    // device that finds nothing reports a missing file. An empty file is a valid
    // "nothing cached yet" placeholder; the Vulkan device overwrites it with real data
    // as soon as pipelines exist. Never clobber an existing device-written cache.
    const std::filesystem::path cacheFile =
        root / kProjectShaderCacheDirectory / kProjectPipelineCacheFileName;
    if (!std::filesystem::exists(cacheFile, directoryError) && !directoryError) {
        std::ofstream stream(cacheFile, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "Cannot create pipeline cache file: " + cacheFile.string();
            std::filesystem::remove_all(root, directoryError);
            return std::nullopt;
        }
    }

    Log::info("ProjectTemplate", "Created new project: %s at %s", name.c_str(),
              root.string().c_str());
    return root;
}

} // namespace engine::editor

#endif // MINI_EDITOR
