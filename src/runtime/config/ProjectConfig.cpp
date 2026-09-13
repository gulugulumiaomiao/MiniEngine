#include "runtime/config/ProjectConfig.h"

#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/JsonTransfer.h"
#include "core/serialization/Transfer.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace engine {
namespace {

// Rejected in project names so a name always maps to a single directory.
constexpr std::string_view kInvalidNameCharacters = "\\/:*?\"<>|";

// Project JSON holds the window override as a plain object: absent key means "no
// override", present key is a direct WindowConfig. The generic optional<T> transfer in
// the serialization framework writes a has_value wrapper, which would leak framing into
// a human-edited project file, so the optional is handled explicitly here.
bool transferWindow(Transfer& archive, std::optional<WindowConfig>& window) {
    if (archive.writing()) {
        if (!window.has_value())
            return true;
        return archive.transfer("window", *window);
    }
    if (!archive.beginObject("window")) {
        // No window override: the editor keeps its current window settings.
        archive.clearError();
        window.reset();
        return true;
    }
    // 探测结束后回到父对象，由 WindowConfig 自己管理实际传输的对象边界。
    if (!archive.endObject())
        return false;
    WindowConfig override;
    if (!archive.transfer("window", override))
        return false;
    window = std::move(override);
    return true;
}

} // namespace

bool ProjectConfig::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("schema_version", schemaVersion) ||
        !archive.transfer("name", name)) {
        return false;
    }
    if (!transferWindow(archive, window))
        return false;
    return archive.transfer("render", render) && archive.endObject();
}

bool ProjectConfig::validate(std::string& error) const {
    if (schemaVersion != 1) {
        error = "Unsupported project schema version";
        return false;
    }
    if (name.empty()) {
        error = "Project name cannot be empty";
        return false;
    }
    if (name.find_first_of(kInvalidNameCharacters) != std::string::npos) {
        error = "Project name contains characters that are invalid in a directory name";
        return false;
    }
    if (window.has_value()) {
        if (window->width == 0 || window->height == 0) {
            error = "Project window dimensions must be valid";
            return false;
        }
    }
    if (render.pipeline.empty()) {
        error = "Project render pipeline name cannot be empty";
        return false;
    }
    return true;
}

std::optional<ProjectConfig> ProjectConfig::load(const std::filesystem::path& path,
                                                 std::string& error) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        error = "Cannot open project configuration: " + path.string();
        return std::nullopt;
    }
    const std::string source{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
    JsonReader reader{source};
    ProjectConfig config;
    if (!reader.valid() || !config.transfer(reader)) {
        error = reader.error().empty() ? "Invalid project configuration" : reader.error();
        return std::nullopt;
    }
    if (!config.validate(error))
        return std::nullopt;
    return config;
}

bool ProjectConfig::save(const std::filesystem::path& path, std::string& error) const {
    if (!validate(error))
        return false;

    JsonWriter writer;
    ProjectConfig copy = *this;
    if (!copy.transfer(writer)) {
        error = "Cannot serialize project configuration";
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        error = "Cannot write project configuration: " + path.string();
        return false;
    }
    const std::string json = writer.toString() + "\n";
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!stream) {
        error = "Cannot write project configuration: " + path.string();
        return false;
    }
    return true;
}

std::filesystem::path projectConfigPath(const std::filesystem::path& projectRoot) {
    return projectRoot / kProjectConfigFileName;
}

std::vector<ProjectMount> projectMounts(const std::filesystem::path& projectRoot) {
    const std::filesystem::path root = std::filesystem::absolute(projectRoot).lexically_normal();
    return {
        {"assets", root / kProjectAssetsDirectory, false},
        {"library", root / kProjectLibraryDirectory, false},
        {"shader-cache", root / kProjectShaderCacheDirectory, false},
        {"shader-bin", root / kProjectShaderBinaryDirectory, true},
    };
}

bool isProjectDirectory(const std::filesystem::path& projectRoot) {
    std::error_code error;
    return std::filesystem::is_regular_file(projectConfigPath(projectRoot), error) && !error;
}

bool ensureProjectMainScene(std::string& error) {
    const VirtualPath sceneDirectory{"assets://scenes"};
    for (const VirtualPath& file : FILE_SYSTEM.listFiles(sceneDirectory, true)) {
        if (file.relativePath().ends_with(".scene.json"))
            return true;
    }

    const VirtualPath source{kBuiltinDefaultScenePath};
    const std::optional<std::string> contents = FILE_SYSTEM.readText(source);
    if (!contents) {
        error = "Cannot read the built-in default Scene: " + source.string();
        return false;
    }
    const VirtualPath target{kProjectMainScenePath};
    if (!FILE_SYSTEM.writeTextAtomic(target, *contents)) {
        error = "Cannot write the project's main Scene: " + target.string();
        return false;
    }
    Log::info("Project", "Created the project's main Scene: %s", target.string().c_str());
    return true;
}

VirtualPath selectProjectScene(const VirtualPath& preferred) {
    if (preferred.valid() && preferred.scheme() == "assets" &&
        preferred.relativePath().ends_with(".scene.json") && FILE_SYSTEM.isFile(preferred)) {
        return preferred;
    }

    const VirtualPath mainScene{kProjectMainScenePath};
    if (FILE_SYSTEM.isFile(mainScene))
        return mainScene;

    std::vector<VirtualPath> scenes;
    for (const VirtualPath& file : FILE_SYSTEM.listFiles(VirtualPath{"assets://scenes"}, true)) {
        if (file.relativePath().ends_with(".scene.json"))
            scenes.push_back(file);
    }
    if (scenes.empty())
        return {};
    std::ranges::sort(scenes, {}, [](const VirtualPath& path) { return path.relativePath(); });
    return scenes.front();
}

} // namespace engine

