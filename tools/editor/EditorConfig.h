#pragma once

#if defined(MINI_EDITOR)

#include "tools/editor/ProjectRegistry.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace engine {

// Editor-only preferences persisted to editor.json. Loaded and saved through a
// physical filesystem path (the path next to the executable) by the editor process.
// The recent-project registry is embedded in this file and is written together with
// the config, so it is never persisted through ProjectRegistry::save() alone.
struct EditorConfig : public Transferable {
    // User window preference. When absent the engine.json default is used.
    struct WindowPreference : public Transferable {
        std::uint32_t width{1280};
        std::uint32_t height{720};
        bool vsync{true};
        std::string name{"Mini Editor"};
        std::int32_t x{-1};   // -1 = system default
        std::int32_t y{-1};
        bool maximized{};

        WindowPreference() : width(1280), height(720), vsync(true), x(-1), y(-1), maximized(false) {}
        bool transfer(Transfer& archive) override;
    };

    std::uint32_t schemaVersion{1};
    std::optional<WindowPreference> window;
    editor::ProjectRegistry registry;

    [[nodiscard]] static std::optional<EditorConfig> load(const std::filesystem::path& path);
    [[nodiscard]] bool save(const std::filesystem::path& path) const;
    [[nodiscard]] static EditorConfig createDefault();
    [[nodiscard]] bool validate(std::string& error) const;

    bool transfer(Transfer& archive) override;
};

} // namespace engine

#endif // MINI_EDITOR
