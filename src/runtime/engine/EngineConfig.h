#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace engine {

class Transfer;

struct AppConfig {
    std::string name{"Application"};
    std::uint32_t width{1280};
    std::uint32_t height{720};
    bool vsync{true};
};

struct RenderConfig {
    std::string pipeline{"MiniForward"};

    bool transfer(Transfer& archive);
};

struct FileSystemMountConfig {
    std::string scheme;
    std::string type{"directory"};
    std::string path;
    bool readOnly{};

    bool transfer(Transfer& archive);
};

struct FileSystemConfig {
    std::vector<FileSystemMountConfig> mounts;

    bool transfer(Transfer& archive);
};

struct EngineConfig {
    std::uint32_t schemaVersion{1};
    std::string workingDirectory{"."};
    AppConfig application;
    RenderConfig render;
    FileSystemConfig filesystem;

    bool transfer(Transfer& archive);
    [[nodiscard]] bool validate(std::string& error) const;
    [[nodiscard]] static std::optional<EngineConfig> load(const std::filesystem::path& path,
                                                          std::string& error);
};

} // namespace engine
