#include "runtime/engine/EngineConfig.h"

#include "core/filesystem/VirtualPath.h"
#include "core/serialization/JsonTransfer.h"
#include "core/serialization/Transfer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace engine {

bool RenderConfig::transfer(Transfer& archive) {
    return archive.transfer("pipeline", pipeline) &&
           archive.transfer("pipeline_cache_path", pipelineCachePath);
}

bool FileSystemMountConfig::transfer(Transfer& archive) {
    return archive.transfer("scheme", scheme) && archive.transfer("type", type) &&
           archive.transfer("path", path) && archive.transfer("read_only", readOnly);
}

bool FileSystemConfig::transfer(Transfer& archive) {
    return archive.transfer("mounts", mounts);
}

bool EngineConfig::transfer(Transfer& archive) {
    if (!archive.transfer("schema_version", schemaVersion) ||
        !archive.transfer("working_directory", workingDirectory) ||
        !archive.beginObject("application") || !archive.transfer("name", application.name) ||
        !archive.transfer("width", application.width) ||
        !archive.transfer("height", application.height) ||
        !archive.transfer("vsync", application.vsync) || !archive.endObject() ||
        !archive.transfer("render", render)) {
        return false;
    }
    return archive.transfer("filesystem", filesystem);
}

bool EngineConfig::validate(std::string& error) const {
    if (schemaVersion != 1) {
        error = "Unsupported engine configuration schema version";
        return false;
    }
    if (workingDirectory.empty()) {
        error = "Engine working directory cannot be empty";
        return false;
    }
    if (application.name.empty() || application.width == 0 || application.height == 0) {
        error = "Application name and window dimensions must be valid";
        return false;
    }
    if (render.pipeline.empty()) {
        error = "Render pipeline name cannot be empty";
        return false;
    }
    if (filesystem.mounts.empty()) {
        error = "The filesystem mount table is empty";
        return false;
    }

    std::unordered_set<std::string> schemes;
    for (const FileSystemMountConfig& mount : filesystem.mounts) {
        std::string scheme = mount.scheme;
        std::ranges::transform(scheme, scheme.begin(), [](char character) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        });
        if (!VirtualPath{scheme + "://"}.valid()) {
            error = "Invalid filesystem mount scheme: " + mount.scheme;
            return false;
        }
        if (!schemes.insert(scheme).second) {
            error = "Duplicate filesystem mount scheme: " + mount.scheme;
            return false;
        }
        if (mount.type != "directory") {
            error = "Unsupported filesystem mount type: " + mount.type;
            return false;
        }
        if (mount.path.empty()) {
            error = "Filesystem mount path cannot be empty: " + mount.scheme;
            return false;
        }
    }

    constexpr const char* requiredSchemes[] = {"asset", "library", "shader-cache", "shader-bin"};
    for (const char* required : requiredSchemes) {
        if (!schemes.contains(required)) {
            error = std::string{"Required filesystem mount is missing: "} + required;
            return false;
        }
    }
    return true;
}

std::optional<EngineConfig> EngineConfig::load(const std::filesystem::path& path,
                                               std::string& error) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        error = "Cannot open engine configuration";
        return std::nullopt;
    }
    const std::string source{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
    JsonReader reader{source};
    EngineConfig config;
    if (!reader.valid() || !reader.beginObject({}) || !config.transfer(reader) ||
        !reader.endObject()) {
        error = reader.error().empty() ? "Invalid engine configuration" : reader.error();
        return std::nullopt;
    }
    if (!config.validate(error)) {
        return std::nullopt;
    }
    return config;
}

} // namespace engine
