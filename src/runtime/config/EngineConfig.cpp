#include "runtime/config/EngineConfig.h"

#include "core/logging/Log.h"
#include "core/serialization/JsonTransfer.h"
#include "core/serialization/Transfer.h"

#include <fstream>
#include <iterator>

namespace engine {

bool WindowConfig::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("width", width) ||
        !archive.transfer("height", height) || !archive.transfer("vsync", vsync))
        return false;
    // 兼容尚无窗口名称的配置，保留类型的默认名称。
    if (!archive.transfer("name", name)) {
        if (archive.writing())
            return false;
        archive.clearError();
    }
    return archive.endObject();
}

bool RenderConfig::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("pipeline", pipeline) &&
           archive.endObject();
}

bool EngineConfig::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("schema_version", schemaVersion) ||
        !archive.transfer("working_directory", workingDirectory) ||
        !archive.transfer("window", window)) {
        return false;
    }
    return archive.transfer("render", render) && archive.endObject();
}

bool EngineConfig::save(const std::filesystem::path& path, std::string& error) const {
    if (!validate(error))
        return false;
    JsonWriter writer;
    EngineConfig copy = *this;
    if (!copy.transfer(writer)) {
        error = "Cannot serialize engine configuration";
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        error = "Cannot write engine configuration: " + path.string();
        return false;
    }
    const std::string json = writer.toString() + "\n";
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    return static_cast<bool>(stream);
}

EngineConfig EngineConfig::createDefault() {
    EngineConfig config;
    config.schemaVersion = 1;
    config.workingDirectory = ".";
    config.window = WindowConfig{1280, 720, true};
    config.render = RenderConfig{"MiniForward"};
    return config;
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
    if (window.width == 0 || window.height == 0) {
        error = "Window dimensions must be valid";
        return false;
    }
    if (render.pipeline.empty()) {
        error = "Render pipeline name cannot be empty";
        return false;
    }
    return true;
}

std::optional<EngineConfig> EngineConfig::load(const std::filesystem::path& path,
                                               std::string& error) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        // File doesn't exist - create default configuration
        Log::info("EngineConfig", "Configuration file not found, creating default: %s", path.string().c_str());
        EngineConfig defaultConfig = createDefault();
        if (!defaultConfig.save(path, error)) {
            return std::nullopt;
        }
        return defaultConfig;
    }
    const std::string source{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
    JsonReader reader{source};
    EngineConfig config;
    if (!reader.valid() || !config.transfer(reader)) {
        error = reader.error().empty() ? "Invalid engine configuration" : reader.error();
        return std::nullopt;
    }
    if (!config.validate(error)) {
        return std::nullopt;
    }
    return config;
}

namespace {
constexpr std::string_view kProjectMountSchemes[] = {
    "assets", "library", "shader-cache", "shader-bin"};
} // namespace

bool isProjectMountScheme(std::string_view scheme) {
    for (std::string_view s : kProjectMountSchemes) {
        if (s == scheme)
            return true;
    }
    return false;
}

} // namespace engine
