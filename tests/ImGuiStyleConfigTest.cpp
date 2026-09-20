// ImVec2/ImVec4 operator== (used by EXPECT_EQ) only comes with the math
// operators enabled; the macro must be set before imgui.h is first included,
// and ImGuiStyleConfig.h below already includes it.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "tools/editor/backend/ImGuiStyleConfig.h"

#include "core/serialization/JsonTransfer.h"
#include "imgui.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using namespace engine::editor;

// ImGuiStyleConfig::load resolves the derived style synchronously, so the tests
// can assert against the returned object; apply() additionally needs a live
// ImGui context. Each test gets both plus an empty temp directory, and no RHI:
// the font atlas builds on the CPU.
class ImGuiStyleConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        ImGui::CreateContext();
        static std::atomic<unsigned> counter{0};
        root_ = std::filesystem::temp_directory_path() / "mini-imgui-style" /
                (std::to_string(counter.fetch_add(1)) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::create_directories(root_);
    }

    void TearDown() override {
        ImGui::DestroyContext();
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] std::filesystem::path stylePath() const { return root_ / "imgui_style.json"; }

    static void writeTextFile(const std::filesystem::path& path, std::string_view content) {
        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    std::filesystem::path root_;
};

// A missing file publishes the full default document (font at 16px, style at
// 16/13 proportions, StyleColorsDark) and reloads to the very same values.
TEST_F(ImGuiStyleConfigTest, MissingFileCreatesDefaultDocument) {
    ASSERT_FALSE(std::filesystem::exists(stylePath()));
    ImGuiStyleConfig config = ImGuiStyleConfig::load(stylePath());
    ASSERT_TRUE(std::filesystem::exists(stylePath()));

    // A relative font.file resolves against the style file's directory (the
    // engine rewrites the process CWD before attach, so that base is the only
    // stable one); the document itself keeps the relative form. The default
    // climbs two levels: editor/config/ -> the editor root's fonts/.
    const std::string resolvedFont =
        (root_.parent_path().parent_path() / "fonts" / "NotoSansSC-Regular.ttf")
            .lexically_normal()
            .string();
    EXPECT_EQ(config.fontFile(), resolvedFont);
    std::ifstream published{stylePath()};
    const std::string document{std::istreambuf_iterator<char>{published},
                               std::istreambuf_iterator<char>{}};
    EXPECT_NE(document.find("../../fonts/NotoSansSC-Regular.ttf"), std::string::npos);
    EXPECT_FLOAT_EQ(config.fontSize(), 16.0F);
    EXPECT_EQ(config.glyphRanges(), "chinese-common");
    // Factory (8,8) scaled by 16/13 truncates to (9,9); Alpha never scales.
    EXPECT_EQ(config.style().WindowPadding, ImVec2(9, 9));
    EXPECT_EQ(config.style().ScrollbarSize, 17.0F);
    EXPECT_EQ(config.style().IndentSpacing, 25.0F);
    EXPECT_FLOAT_EQ(config.style().Alpha, 1.0F);

    // The generated document lists every field explicitly, so a reload must
    // round-trip to identical values.
    ImGuiStyleConfig reloaded = ImGuiStyleConfig::load(stylePath());
    EXPECT_EQ(reloaded.style().WindowPadding, config.style().WindowPadding);
    EXPECT_EQ(reloaded.style().ScrollbarSize, config.style().ScrollbarSize);
    EXPECT_FLOAT_EQ(reloaded.fontSize(), config.fontSize());
    EXPECT_EQ(reloaded.glyphRanges(), config.glyphRanges());
    EXPECT_EQ(reloaded.fontFile(), resolvedFont);
}

// A hand-written partial document is legal: fields the file lists override the
// derived values, everything missing keeps the font-size-proportioned default.
TEST_F(ImGuiStyleConfigTest, PartialDocumentKeepsDerivedDefaults) {
    writeTextFile(stylePath(), R"json({
      "font": { "size": 20.0 },
      "style": { "item_spacing": [30, 10] }
    })json");

    ImGuiStyleConfig config = ImGuiStyleConfig::load(stylePath());
    EXPECT_FLOAT_EQ(config.fontSize(), 20.0F);
    EXPECT_EQ(config.style().ItemSpacing, ImVec2(30, 10)); // explicit override
    // Factory (8,8) scaled by 20/13 truncates to (12,12); not listed in the file.
    EXPECT_EQ(config.style().WindowPadding, ImVec2(12, 12));
    // Font defaults kept, resolved against the style file's directory.
    EXPECT_EQ(config.fontFile(),
              (root_.parent_path().parent_path() / "fonts" / "NotoSansSC-Regular.ttf")
                  .lexically_normal()
                  .string());
    // Colors section absent: the factory StyleColorsDark palette stays.
    EXPECT_FLOAT_EQ(config.style().Colors[ImGuiCol_WindowBg].x, 0.06F);
    EXPECT_FLOAT_EQ(config.style().Colors[ImGuiCol_WindowBg].w, 0.94F);
}

// Fields ImGui does not scale (Alpha, global switches) keep their factory
// values, while sizes follow the font size.
TEST_F(ImGuiStyleConfigTest, ScaleMovesSizesButNotAlpha) {
    writeTextFile(stylePath(), R"json({ "font": { "size": 26.0 } })json");
    ImGuiStyleConfig config = ImGuiStyleConfig::load(stylePath());
    // (4,3) scaled by 26/13 = (8,6); DisabledAlpha is not a size and never scales.
    EXPECT_EQ(config.style().FramePadding, ImVec2(8, 6));
    EXPECT_FLOAT_EQ(config.style().DisabledAlpha, 0.60F);
    EXPECT_TRUE(config.style().AntiAliasedFill);
}

// A single color entry overrides only that entry.
TEST_F(ImGuiStyleConfigTest, ColorOverrideApplies) {
    writeTextFile(stylePath(), R"json({ "colors": { "Text": [1, 0, 0, 1] } })json");
    ImGuiStyleConfig config = ImGuiStyleConfig::load(stylePath());
    EXPECT_EQ(config.style().Colors[ImGuiCol_Text], ImVec4(1, 0, 0, 1));
    EXPECT_FLOAT_EQ(config.style().Colors[ImGuiCol_WindowBg].x, 0.06F); // untouched
}

// Invalid JSON yields the default configuration instead of failing; the broken
// file is left alone for the user to fix.
TEST_F(ImGuiStyleConfigTest, BrokenJsonYieldsDefaults) {
    writeTextFile(stylePath(), R"json({ "font": "oops")json");
    ImGuiStyleConfig config = ImGuiStyleConfig::load(stylePath());
    EXPECT_FLOAT_EQ(config.fontSize(), 16.0F);
    EXPECT_EQ(config.style().WindowPadding, ImVec2(9, 9));
}

// The vendored Noto Sans SC loads through the full pipeline: the pre-flight
// accepts it, the atlas builds on the CPU and the common simplified-Chinese
// ranges really rasterize a CJK glyph. The font is referenced by its absolute
// source-tree path, so the test does not depend on the build-tree copy or the
// process working directory.
TEST_F(ImGuiStyleConfigTest, AppliesVendoredChineseFont) {
    const std::string fontPath =
        (std::filesystem::path{MINI_TEST_FONT_DIR} / "NotoSansSC-Regular.ttf").generic_string();
    writeTextFile(stylePath(), "{ \"font\": { \"file\": \"" + fontPath +
                                   "\", \"size\": 16.0, \"glyph_ranges\": \"chinese-common\" } }");
    ImGuiStyleConfig::load(stylePath()).apply();

    ImGuiIO& io = ImGui::GetIO();
    ASSERT_EQ(io.Fonts->Fonts.Size, 1);
    ASSERT_TRUE(io.Fonts->Build());
    EXPECT_EQ(io.Fonts->Fonts[0]->FontSize, 16);
    // U+4E2D '中' must exist when the chinese-common ranges were rasterized.
    EXPECT_NE(io.Fonts->Fonts[0]->FindGlyphNoFallback(0x4E2D), nullptr);
}

// The default document's relative font path resolves against the style file's
// directory; where no fonts/ sits there, apply() falls back to the built-in
// font instead of failing (the editor always starts).
TEST_F(ImGuiStyleConfigTest, UnresolvedDefaultFontFallsBack) {
    ImGuiStyleConfig::load(stylePath()).apply(); // publishes the default document

    ImGuiIO& io = ImGui::GetIO();
    ASSERT_EQ(io.Fonts->Fonts.Size, 1);
    EXPECT_EQ(io.Fonts->Sources[0].SizePixels, 13);
}

// A font file that is missing or not TrueType falls back to the built-in 13px
// font together with the factory style, so proportions stay consistent. The
// non-TrueType case exercises the pre-flight on an absolute path (a broken file
// would otherwise assert inside imgui's atlas builder).
TEST_F(ImGuiStyleConfigTest, UnusableFontFallsBackToFactoryDefaults) {
    const std::filesystem::path fakeFont = root_ / "not-a-font.ttf";
    writeTextFile(fakeFont, "this is definitely not a truetype font");
    const std::string missing = (root_ / "missing.ttf").generic_string();

    for (const std::string& file : {missing, fakeFont.generic_string()}) {
        SCOPED_TRACE(file);
        writeTextFile(stylePath(),
                      "{ \"font\": { \"file\": \"" + file + "\", \"size\": 18.0 } }");
        // AddFont* is additive on the atlas; each round starts from an empty
        // one, matching the fresh-context timing of a real attach.
        ImGui::GetIO().Fonts->ClearFonts();
        ImGuiStyleConfig::load(stylePath()).apply();

        ImGuiIO& io = ImGui::GetIO();
        // AddFontDefault installed one font; the style is the unscaled factory
        // style that matches the 13px built-in font.
        ASSERT_EQ(io.Fonts->Fonts.Size, 1);
        // The atlas has not been built yet (the renderer does that), so the
        // pixel size still lives in the atlas-level font sources.
        EXPECT_EQ(io.Fonts->Sources[0].SizePixels, 13);
        EXPECT_EQ(ImGui::GetStyle().WindowPadding, ImVec2(8, 8));
    }
}

} // namespace
