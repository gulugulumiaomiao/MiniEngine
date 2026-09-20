#pragma once

#include "imgui.h"

#include <filesystem>
#include <string>

namespace engine {

class Transfer;

namespace editor {

// The editor's ImGui appearance, as persisted in editor/config/imgui_style.json:
// the font (file, size, glyph ranges, oversampling) and every ImGuiStyle display
// field (paddings, spacings, roundings, sizes, colors).
//
// load() resolves the composition once: ImGui's factory style (whose colors are
// StyleColorsDark) scaled by fontSize / 13 (the built-in font's pixel size), then
// the fields the file lists explicitly override the scaled values last. Fields or
// whole sections may be missing from the file — a hand-written half document is
// valid and the missing entries simply keep their derived defaults. A file that
// fails to parse yields the default configuration instead of an error.
//
// A relative font.file is resolved against the style file's own directory. The
// engine rewrites the process CWD to the working_directory declared by
// engine.json during initialize — before attach() runs — so resolving against
// the CWD there would look in the wrong place; the file-local base keeps the
// default font (../../fonts/, the editor root next to editor/config/) working
// wherever the engine is rooted.
//
// apply() is the consumer: it uploads the font into the live atlas and assigns
// the style. It must run after ImGui::CreateContext and before the atlas builds
// (ImGuiLayer::attach drives that order, re-running it on every project switch so
// editing the file takes effect without restarting the process).
class ImGuiStyleConfig {
public:
    // Reads the file; when it does not exist yet, a complete default document is
    // written first so the file can be hand-edited as a reference.
    static ImGuiStyleConfig load(const std::filesystem::path& path);

    // Installs the font and the style into the current ImGui context. A font
    // file that cannot be loaded falls back to ImGui's built-in 13px font and
    // the factory style (proportions stay consistent with the actual font).
    void apply() const;

    // Introspection for tests. fontFile() is the resolved absolute path after
    // load(); the document on disk keeps the hand-editable relative form.
    [[nodiscard]] const ImGuiStyle& style() const { return style_; }
    [[nodiscard]] const std::string& fontFile() const { return font_.file; }
    [[nodiscard]] float fontSize() const { return font_.size; }
    [[nodiscard]] const std::string& glyphRanges() const { return font_.glyphRanges; }

private:
    struct Font {
        std::string file{"../../fonts/NotoSansSC-Regular.ttf"};
        float size{16.0F};
        std::string glyphRanges{"chinese-common"};
        int oversampleH{2};
        int oversampleV{1};
        bool pixelSnapH{true};
    };

    // Field-transfer halves shared by load() and save(); reading is tolerant
    // (absent fields keep their current values) and writing must succeed.
    bool transferFont(Transfer& archive);
    bool transferStyle(Transfer& archive);
    bool transferColors(Transfer& archive);

    [[nodiscard]] bool save(const std::filesystem::path& path) const;

    Font font_;
    ImGuiStyle style_{}; // constructor value = ImGui defaults + StyleColorsDark
};

} // namespace editor
} // namespace engine
