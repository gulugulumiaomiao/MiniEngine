#include "tools/editor/backend/ImGuiStyleConfig.h"

#include "core/logging/Log.h"
#include "core/math/Math.h"
#include "core/serialization/JsonTransfer.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace engine::editor {
namespace {

// ImGui's built-in ProggyClean font is a 13px bitmap font; the default style's
// paddings and sizes are proportioned for it. Scaling by fontSize / 13 keeps the
// default proportions coherent at other sizes before explicit overrides apply.
constexpr float kBaseFontSize = 13.0F;

// ImGuiStyle display fields, snake_case for the JSON document. Every entry is
// read tolerantly (a missing or wrongly typed field keeps the current value) and
// written by the default-document generator, so both directions share one table
// and cannot drift apart.
struct FloatField {
    const char* name;
    float ImGuiStyle::*field;
};
struct Vec2Field {
    const char* name;
    ImVec2 ImGuiStyle::*field;
};
struct BoolField {
    const char* name;
    bool ImGuiStyle::*field;
};
struct DirField {
    const char* name;
    ImGuiDir ImGuiStyle::*field;
};

constexpr FloatField kFloatFields[] = {
    {"alpha", &ImGuiStyle::Alpha},
    {"disabled_alpha", &ImGuiStyle::DisabledAlpha},
    {"window_rounding", &ImGuiStyle::WindowRounding},
    {"window_border_size", &ImGuiStyle::WindowBorderSize},
    {"window_border_hover_padding", &ImGuiStyle::WindowBorderHoverPadding},
    {"child_rounding", &ImGuiStyle::ChildRounding},
    {"child_border_size", &ImGuiStyle::ChildBorderSize},
    {"popup_rounding", &ImGuiStyle::PopupRounding},
    {"popup_border_size", &ImGuiStyle::PopupBorderSize},
    {"frame_rounding", &ImGuiStyle::FrameRounding},
    {"frame_border_size", &ImGuiStyle::FrameBorderSize},
    {"indent_spacing", &ImGuiStyle::IndentSpacing},
    {"columns_min_spacing", &ImGuiStyle::ColumnsMinSpacing},
    {"scrollbar_size", &ImGuiStyle::ScrollbarSize},
    {"scrollbar_rounding", &ImGuiStyle::ScrollbarRounding},
    {"grab_min_size", &ImGuiStyle::GrabMinSize},
    {"grab_rounding", &ImGuiStyle::GrabRounding},
    {"log_slider_deadzone", &ImGuiStyle::LogSliderDeadzone},
    {"image_border_size", &ImGuiStyle::ImageBorderSize},
    {"tab_rounding", &ImGuiStyle::TabRounding},
    {"tab_border_size", &ImGuiStyle::TabBorderSize},
    {"tab_close_button_min_width_selected", &ImGuiStyle::TabCloseButtonMinWidthSelected},
    {"tab_close_button_min_width_unselected", &ImGuiStyle::TabCloseButtonMinWidthUnselected},
    {"tab_bar_border_size", &ImGuiStyle::TabBarBorderSize},
    {"tab_bar_overline_size", &ImGuiStyle::TabBarOverlineSize},
    {"table_angled_headers_angle", &ImGuiStyle::TableAngledHeadersAngle},
    {"separator_text_border_size", &ImGuiStyle::SeparatorTextBorderSize},
    {"docking_separator_size", &ImGuiStyle::DockingSeparatorSize},
    {"mouse_cursor_scale", &ImGuiStyle::MouseCursorScale},
    {"curve_tessellation_tol", &ImGuiStyle::CurveTessellationTol},
    {"circle_tessellation_max_error", &ImGuiStyle::CircleTessellationMaxError},
};

constexpr Vec2Field kVec2Fields[] = {
    {"window_padding", &ImGuiStyle::WindowPadding},
    {"window_min_size", &ImGuiStyle::WindowMinSize},
    {"window_title_align", &ImGuiStyle::WindowTitleAlign},
    {"frame_padding", &ImGuiStyle::FramePadding},
    {"item_spacing", &ImGuiStyle::ItemSpacing},
    {"item_inner_spacing", &ImGuiStyle::ItemInnerSpacing},
    {"cell_padding", &ImGuiStyle::CellPadding},
    {"touch_extra_padding", &ImGuiStyle::TouchExtraPadding},
    {"table_angled_headers_text_align", &ImGuiStyle::TableAngledHeadersTextAlign},
    {"button_text_align", &ImGuiStyle::ButtonTextAlign},
    {"selectable_text_align", &ImGuiStyle::SelectableTextAlign},
    {"separator_text_align", &ImGuiStyle::SeparatorTextAlign},
    {"separator_text_padding", &ImGuiStyle::SeparatorTextPadding},
    {"display_window_padding", &ImGuiStyle::DisplayWindowPadding},
    {"display_safe_area_padding", &ImGuiStyle::DisplaySafeAreaPadding},
};

constexpr BoolField kBoolFields[] = {
    {"anti_aliased_lines", &ImGuiStyle::AntiAliasedLines},
    {"anti_aliased_lines_use_tex", &ImGuiStyle::AntiAliasedLinesUseTex},
    {"anti_aliased_fill", &ImGuiStyle::AntiAliasedFill},
};

constexpr DirField kDirFields[] = {
    {"window_menu_button_position", &ImGuiStyle::WindowMenuButtonPosition},
    {"color_button_position", &ImGuiStyle::ColorButtonPosition},
};

[[nodiscard]] const ImWchar* glyphRangesFor(const std::string& name) {
    // The GetGlyphRanges* helpers are ImFontAtlas members; apply() runs with a
    // live context, so reaching the atlas through IO here is fine.
    if (name == "default")
        return nullptr;
    if (name == "chinese-common")
        return ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon();
    Log::warn("ImGuiStyleConfig", "Unknown glyph_ranges \"%s\", using the default ranges",
              name.c_str());
    return nullptr;
}

// Pre-flight for the font file: imgui's stb_truetype rasterizer only understands
// TrueType outlines. Feeding it an OpenType/CFF font ('OTTO') or a broken file
// would assert inside the atlas build; rejecting such files here turns that into
// the graceful built-in-font fallback instead.
[[nodiscard]] bool looksLikeTrueTypeFile(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        return false;
    std::uint8_t magic[4]{};
    if (!stream.read(reinterpret_cast<char*>(magic), sizeof(magic)))
        return false;
    const std::uint32_t tag = (std::uint32_t{magic[0]} << 24) | (std::uint32_t{magic[1]} << 16) |
                              (std::uint32_t{magic[2]} << 8) | std::uint32_t{magic[3]};
    constexpr std::uint32_t kTrueType = 0x00010000U; // version 1.0 sfnt
    constexpr std::uint32_t kTrueTag = 0x74727565U;  // 'true', rare TrueType-flavored tag
    constexpr std::uint32_t kCollectionTag = 0x74646366U; // 'ttcf', font collection
    return tag == kTrueType || tag == kTrueTag || tag == kCollectionTag;
}

// Reading: a failed transfer means the field is absent or wrongly typed; clear
// the error and keep the member's current value. Writing: failure is real and
// aborts the document.
template <typename Value, typename TransferT, typename Set>
bool tolerantField(TransferT& archive, const char* name, Value& value, Set set) {
    if (archive.transfer(name, value)) {
        set(value);
        return true;
    }
    if (archive.reading()) {
        archive.clearError();
        return true;
    }
    return false;
}

// Opens a named section (font/style/colors). Reading tolerates a missing section
// by skipping it entirely; writing must succeed.
template <typename TransferT, typename Body>
bool section(TransferT& archive, const char* name, Body body) {
    if (archive.beginObject(name)) {
        const bool done = body();
        return done && archive.endObject();
    }
    if (archive.reading()) {
        archive.clearError();
        return true;
    }
    return false;
}

// The document keeps font.file in its hand-editable form, but the engine has
// rewritten the process CWD to engine.json's working_directory by the time
// attach() runs, so a relative font path must be pinned before apply() uses it.
// Absolute paths pass through untouched.
void resolveFontFile(std::string& file, const std::filesystem::path& stylePath) {
    const std::filesystem::path fontPath{file};
    if (fontPath.is_absolute())
        return;
    file = (stylePath.parent_path() / fontPath).lexically_normal().string();
}

} // namespace

bool ImGuiStyleConfig::transferFont(Transfer& archive) {
    if (archive.reading()) {
        return tolerantField(archive, "file", font_.file, [](const std::string&) {}) &&
               tolerantField(archive, "size", font_.size, [](float) {}) &&
               tolerantField(archive, "glyph_ranges", font_.glyphRanges,
                             [](const std::string&) {}) &&
               tolerantField(archive, "oversample_h", font_.oversampleH, [](std::int32_t) {}) &&
               tolerantField(archive, "oversample_v", font_.oversampleV, [](std::int32_t) {}) &&
               tolerantField(archive, "pixel_snap_h", font_.pixelSnapH, [](bool) {});
    }
    return archive.transfer("file", font_.file) && archive.transfer("size", font_.size) &&
           archive.transfer("glyph_ranges", font_.glyphRanges) &&
           archive.transfer("oversample_h", font_.oversampleH) &&
           archive.transfer("oversample_v", font_.oversampleV) &&
           archive.transfer("pixel_snap_h", font_.pixelSnapH);
}

bool ImGuiStyleConfig::transferStyle(Transfer& archive) {
    for (const FloatField& entry : kFloatFields)
        if (!tolerantField(archive, entry.name, style_.*(entry.field), [](float) {}))
            return false;
    for (const Vec2Field& entry : kVec2Fields) {
        math::Vec2 value{(style_.*(entry.field)).x, (style_.*(entry.field)).y};
        if (!tolerantField(archive, entry.name, value, [&](const math::Vec2& v) {
                style_.*(entry.field) = ImVec2(v.x, v.y);
            }))
            return false;
    }
    for (const BoolField& entry : kBoolFields)
        if (!tolerantField(archive, entry.name, style_.*(entry.field), [](bool) {}))
            return false;
    for (const DirField& entry : kDirFields)
        if (!tolerantField(archive, entry.name, style_.*(entry.field), [](ImGuiDir) {}))
            return false;
    return true;
}

bool ImGuiStyleConfig::transferColors(Transfer& archive) {
    for (int index = 0; index < ImGuiCol_COUNT; ++index) {
        const ImVec4& current = style_.Colors[index];
        math::Vec4 value{current.x, current.y, current.z, current.w};
        if (!tolerantField(archive, ImGui::GetStyleColorName(index), value,
                           [&](const math::Vec4& v) {
                               style_.Colors[index] = ImVec4(v.x, v.y, v.z, v.w);
                           }))
            return false;
    }
    return true;
}

ImGuiStyleConfig ImGuiStyleConfig::load(const std::filesystem::path& path) {
    ImGuiStyleConfig config;

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        // First run: publish a complete default document the user can hand-edit.
        // save() writes through a copy, so the document keeps the relative
        // font.file form; only the returned config carries the pinned path.
        Log::info("ImGuiStyleConfig", "Style file not found, creating default: %s",
                  path.string().c_str());
        config.style_.ScaleAllSizes(config.font_.size / kBaseFontSize);
        if (!config.save(path))
            Log::error("ImGuiStyleConfig", "Cannot write the default style file to %s",
                       path.string().c_str());
        resolveFontFile(config.font_.file, path);
        return config;
    }
    const std::string source{std::istreambuf_iterator<char>{stream},
                             std::istreambuf_iterator<char>{}};
    JsonReader reader{source};
    if (!reader.valid() || !reader.beginObject({})) {
        Log::error("ImGuiStyleConfig", "Style file is not valid JSON: %s", path.string().c_str());
        config.style_.ScaleAllSizes(config.font_.size / kBaseFontSize);
        resolveFontFile(config.font_.file, path);
        return config;
    }

    std::uint32_t schemaVersion = 1;
    if (!reader.transfer("schema_version", schemaVersion))
        reader.clearError(); // absent or unreadable: assume the only known version
    if (schemaVersion != 1) {
        Log::error("ImGuiStyleConfig", "Unsupported style schema version %u in %s", schemaVersion,
                   path.string().c_str());
        config.style_.ScaleAllSizes(config.font_.size / kBaseFontSize);
        resolveFontFile(config.font_.file, path);
        return config;
    }

    // Font first: its size drives the proportion scale applied to the factory
    // style before the explicit style/colors overrides are read on top.
    section(reader, "font", [&] { return config.transferFont(reader); });
    config.style_.ScaleAllSizes(config.font_.size / kBaseFontSize);
    section(reader, "style", [&] { return config.transferStyle(reader); });
    section(reader, "colors", [&] { return config.transferColors(reader); });
    reader.endObject();
    resolveFontFile(config.font_.file, path);
    return config;
}

bool ImGuiStyleConfig::save(const std::filesystem::path& path) const {
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);

    // EditorConfig::save pattern: the tolerant transfers need mutable members,
    // so write through a copy — the writing direction never modifies state.
    ImGuiStyleConfig copy = *this;
    JsonWriter writer;
    if (!writer.beginObject({}))
        return false;
    std::uint32_t schemaVersion = 1;
    if (!writer.transfer("schema_version", schemaVersion))
        return false;
    if (!section(writer, "font", [&] { return copy.transferFont(writer); }))
        return false;
    if (!section(writer, "style", [&] { return copy.transferStyle(writer); }))
        return false;
    if (!section(writer, "colors", [&] { return copy.transferColors(writer); }))
        return false;
    if (!writer.endObject())
        return false;

    const std::string json = writer.toString() + "\n";
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        Log::error("ImGuiStyleConfig", "Cannot write the style file to %s", path.string().c_str());
        return false;
    }
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    return static_cast<bool>(out);
}

void ImGuiStyleConfig::apply() const {
    IM_ASSERT(ImGui::GetCurrentContext() != nullptr);
    ImGuiIO& io = ImGui::GetIO();

    // font.file is UTF-8 from the JSON document, pinned to an absolute path by
    // load() (the engine rewrites the process CWD to engine.json's
    // working_directory before attach runs, so a CWD-relative lookup would miss).
    // The pre-flight rejects missing files and non-TrueType fonts so the atlas
    // build never sees data it would assert on.
    const std::filesystem::path fontPath{
        std::u8string{font_.file.begin(), font_.file.end()}};
    if (!font_.file.empty() && looksLikeTrueTypeFile(fontPath)) {
        ImFontConfig config;
        config.OversampleH = font_.oversampleH;
        config.OversampleV = font_.oversampleV;
        config.PixelSnapH = font_.pixelSnapH;
        if (io.Fonts->AddFontFromFileTTF(font_.file.c_str(), font_.size, &config,
                                         glyphRangesFor(font_.glyphRanges)) != nullptr) {
            ImGui::GetStyle() = style_;
            return;
        }
        Log::error("ImGuiStyleConfig", "Cannot load the font %s (path encoding is ANSI in ImGui)",
                   font_.file.c_str());
    } else if (!font_.file.empty()) {
        Log::error("ImGuiStyleConfig", "The font file %s is missing or not a TrueType font",
                   font_.file.c_str());
    }

    // Fallback: the built-in 13px bitmap font with the factory style, whose
    // proportions match it. A partially applied style (16px-proportioned sizes
    // around a 13px font) would look worse than the plain default.
    io.Fonts->AddFontDefault();
    ImGui::GetStyle() = ImGuiStyle{};
}

} // namespace engine::editor
