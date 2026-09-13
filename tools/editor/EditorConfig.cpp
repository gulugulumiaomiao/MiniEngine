#include "tools/editor/EditorConfig.h"

#if defined(MINI_EDITOR)

#include <fstream>
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/JsonTransfer.h"
#include "core/serialization/Transfer.h"

namespace engine {

bool EditorConfig::WindowPreference::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("width", width) ||
        !archive.transfer("height", height) || !archive.transfer("vsync", vsync) ||
        !archive.transfer("x", x) || !archive.transfer("y", y) ||
        !archive.transfer("maximized", maximized))
        return false;
    // 旧 editor.json 缺少名称时沿用 Mini Editor。
    if (!archive.transfer("name", name)) {
        if (archive.writing())
            return false;
        archive.clearError();
    }
    return archive.endObject();
}

bool EditorConfig::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("schema_version", schemaVersion))
        return false;
    // Serialize window directly without optional wrapper
    if (archive.writing()) {
        if (window.has_value()) {
            if (!archive.transfer("window", *window))
                return false;
        } else {
            // Write null/empty object for missing window
            if (!archive.beginObject("window"))
                return false;
            if (!archive.endObject())
                return false;
        }
    } else {
        // Reading: always try to read window object
        WindowPreference wp;
        if (archive.transfer("window", wp)) {
            window = wp;
        } else {
            // 可选窗口缺失或为空时保留默认值；字段作用域已恢复到父对象。
            archive.clearError();
            window.reset();
        }
    }
    return archive.transfer("registry", registry) && archive.endObject();
}

bool EditorConfig::save(const std::filesystem::path& path) const {
    std::string validationError;
    if (!validate(validationError)) {
        Log::error("EditorConfig", "Cannot save editor config: %s", validationError.c_str());
        return false;
    }
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);

    JsonWriter writer;
    EditorConfig copy = *this;
    if (!copy.transfer(writer))
        return false;
    const std::string json = writer.toString() + "\n";
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if (!stream) {
        Log::error("EditorConfig", "Cannot write editor config to %s", path.string().c_str());
        return false;
    }
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    return static_cast<bool>(stream);
}

EditorConfig EditorConfig::createDefault() {
    EditorConfig config;
    config.schemaVersion = 1;
    WindowPreference wp;
    wp.width = 1600;
    wp.height = 900;
    wp.vsync = true;
    wp.x = -1;
    wp.y = -1;
    wp.maximized = false;
    config.window = wp;
    return config;
}

bool EditorConfig::validate(std::string& error) const {
    if (schemaVersion != 1) {
        error = "Unsupported editor configuration schema version";
        return false;
    }
    return true;
}

std::optional<EditorConfig> EditorConfig::load(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        // File doesn't exist - create default configuration
        Log::info("EditorConfig", "Configuration file not found, creating default: %s", path.string().c_str());
        EditorConfig defaultConfig = createDefault();
        if (!defaultConfig.save(path)) {
            return std::nullopt;
        }
        return defaultConfig;
    }
    const std::string source{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
    JsonReader reader{source};
    if (!reader.valid())
        return std::nullopt;
    EditorConfig config;
    if (!config.transfer(reader))
        return std::nullopt;
    std::string validationError;
    if (!config.validate(validationError)) {
        Log::error("EditorConfig", "Invalid editor config: %s", validationError.c_str());
        return std::nullopt;
    }
    config.registry.pruneInvalid();
    return config;
}

} // namespace engine

#endif // MINI_EDITOR
