#pragma once

#include "core/filesystem/VirtualPath.h"
#include "core/serialization/Transferable.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace engine {

class Transfer;

// Window configuration shared by engine.json (default), editor.json (user preference)
// and project.json (project override).
struct WindowConfig : public Transferable {
    std::uint32_t width{1280};
    std::uint32_t height{720};
    bool vsync{true};

    WindowConfig() = default;
    WindowConfig(std::uint32_t width, std::uint32_t height, bool vsync = true)
        : width(width), height(height), vsync(vsync) {}

    bool transfer(Transfer& archive) override;
    [[nodiscard]] bool operator==(const WindowConfig& other) const {
        return width == other.width && height == other.height && vsync == other.vsync;
    }
    [[nodiscard]] bool operator!=(const WindowConfig& other) const {
        return !(*this == other);
    }
};

struct RenderConfig : public Transferable {
    std::string pipeline{"MiniForward"};
    // Virtual path such as "shader-cache://pipeline_cache.bin". Empty or
    // invalid disables persistence and keeps the cache in memory only.
    // Optional when reading: a configuration without this field (e.g. an older
    // project.json) keeps the default instead of failing.
    VirtualPath pipelineCachePath{"shader-cache://pipeline_cache.bin"};

    RenderConfig() = default;
    RenderConfig(std::string pipeline, VirtualPath pipelineCachePath)
        : pipeline(std::move(pipeline)), pipelineCachePath(std::move(pipelineCachePath)) {}

    bool transfer(Transfer& archive) override;
};

struct EngineConfig : public Transferable {
    std::uint32_t schemaVersion{1};
    std::string workingDirectory{"."};
    WindowConfig window;
    RenderConfig render;

    bool transfer(Transfer& archive) override;
    [[nodiscard]] bool validate(std::string& error) const;
    [[nodiscard]] static std::optional<EngineConfig> load(const std::filesystem::path& path,
                                                          std::string& error);
    [[nodiscard]] bool save(const std::filesystem::path& path, std::string& error) const;
    [[nodiscard]] static EngineConfig createDefault();
};

// Returns true when the scheme belongs to the project mount set
// (assets, library, shader-cache, shader-bin).
[[nodiscard]] bool isProjectMountScheme(std::string_view scheme);

} // namespace engine
