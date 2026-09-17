#include "tools/editor/ProjectPanel.h"
#include "asset/base/AssetMeta.h"
#include "asset/database/AssetDatabase.h"
#include "asset/importer/AssetImportPipeline.h"
#include "core/filesystem/FileSystem.h"
#include "core/filesystem/FileWatcher.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "TestAssetEnvironment.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
using namespace engine;
using namespace engine::editor;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                           \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

constexpr std::string_view kShaderJson = R"json({
  "$schemaVersion": 1,
  "name": "PanelShader",
  "properties": [
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] }
  ],
  "subShaders": [{
    "passes": [{
      "name": "Forward",
      "program": { "vertex": "simple.vert", "frag": "simple.frag" }
    }]
  }]
})json";

constexpr std::string_view kSceneJson = R"json({
  "$schemaVersion": 1,
  "name": "Demo",
  "nodes": []
})json";

// 直接向 ImGui 注入输入事件驱动 Project 面板，不创建窗口或 GPU。列表行距为
// GetTextLineHeightWithSpacing（Selectable 提交 label 高度 + ItemSpacing），
// 行位置由 "Project/##content" 子窗口的 CursorStartPos 推得。
struct Harness {
    ProjectPanel panel{[this](const VirtualPath& path) { openedScenes.push_back(path); }};
    std::vector<VirtualPath> openedScenes;
    std::filesystem::path root;
    std::filesystem::path assets;
    bool valid{};

    Harness() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {900, 600};
        io.DeltaTime = 1.0F / 60.0F;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        root = std::filesystem::temp_directory_path() /
               ("MiniEngineProjectPanelTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        assets = root / "assets";
        std::error_code error;
        std::filesystem::create_directories(assets, error);
        valid = !error && test::initializeAssetEnvironment(assets);
        if (!valid)
            return;
        FILE_WATCHER.stop();

        valid = FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.vert"},
                                       "#version 450\nvoid main(){gl_Position=vec4(0);}\n") &&
                FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/simple.frag"},
                                      "#version 450\nlayout(location=0) out vec4 c;"
                                      "void main(){c=vec4(1);}\n") &&
                FILE_SYSTEM.writeText(VirtualPath{"assets://shaders/fixture.shader.json"},
                                      std::string{kShaderJson}) &&
                FILE_SYSTEM.writeText(VirtualPath{"assets://scenes/demo.scene.json"},
                                      std::string{kSceneJson});
        panel.model().setViewMode(ProjectBrowserModel::ViewMode::List);
    }
    ~Harness() {
        FILE_WATCHER.stop();
        test::shutdownAssetEnvironment();
        ImGui::DestroyContext();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({900, 600});
        panel.draw();
        ImGui::Render();
    }
    void settle() {
        frame();
        frame();
    }
    void advance(int frames = 22) {
        for (int i = 0; i < frames; ++i)
            frame();
    }
    [[nodiscard]] ImVec2 contentStart() const {
        // BeginChildEx appends the child's ID hash to the window name
        // ("Project/##content_5E8E87E9"), so locate it by prefix. The exact
        // name would depend on the ID stack at the BeginChild call site.
        const ImGuiContext& context = *ImGui::GetCurrentContext();
        for (const ImGuiWindow* window : context.Windows)
            if (std::strncmp(window->Name, "Project/##content", 17) == 0)
                return window->DC.CursorStartPos;
        return ImVec2{0, 0};
    }
    [[nodiscard]] ImVec2 listRow(int index) const {
        const ImVec2 start = contentStart();
        // Measured row pitch: the Selectable's half-spacing padding absorbs the
        // ItemSpacing gap, so consecutive rows sit GetTextLineHeightWithSpacing()
        // apart (verified by HoveredId scanning; +8 lands mid-row).
        const float pitch = ImGui::GetTextLineHeightWithSpacing();
        return {start.x + 30.0F, start.y + static_cast<float>(index) * pitch + 8.0F};
    }
    [[nodiscard]] ImVec2 toolbarStart() const {
        const ImGuiWindow* window = ImGui::FindWindowByName("Project");
        return window ? window->DC.CursorStartPos : ImVec2{0, 0};
    }
    [[nodiscard]] ImVec2 treeStart() const {
        // Same prefix matching as contentStart: the tree child window name
        // carries the ID hash suffix ("Project/##tree_XXXXXXXX").
        const ImGuiContext& context = *ImGui::GetCurrentContext();
        for (const ImGuiWindow* window : context.Windows)
            if (std::strncmp(window->Name, "Project/##tree", 14) == 0)
                return window->DC.CursorStartPos;
        return ImVec2{0, 0};
    }
    void move(ImVec2 position) {
        ImGui::GetIO().AddMousePosEvent(position.x, position.y);
        frame();
    }
    void button(int button, bool down) {
        ImGui::GetIO().AddMouseButtonEvent(button, down);
        frame();
    }
    void click(ImVec2 position, int mouseButton = 0) {
        move(position);
        button(mouseButton, true);
        button(mouseButton, false);
        settle();
    }
    void dblclick(ImVec2 position) {
        click(position);
        click(position);
    }
    void drag(ImVec2 from, ImVec2 to) {
        advance(); // 不把拖动起点的单击误合并成双击。
        move(from);
        button(0, true);
        move({from.x + 10.0F, from.y});
        move(to);
        settle();
        button(0, false);
        settle();
    }
    bool menuItem(int index) {
        auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        if (popups.empty() || !popups.back().Window)
            return false;
        const auto start = popups.back().Window->DC.CursorStartPos;
        click({start.x + 35,
               start.y + static_cast<float>(index) * ImGui::GetTextLineHeightWithSpacing() + 6});
        return true;
    }
    void type(const char* text) {
        for (const char* character = text; *character; ++character) {
            ImGui::GetIO().AddInputCharacter(*character);
            frame();
        }
    }
    void key(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        frame();
    }
    void modClick(ImGuiKey modifier, ImVec2 position) {
        move(position);
        ImGui::GetIO().AddKeyEvent(modifier, true);
        frame();
        button(0, true);
        button(0, false);
        ImGui::GetIO().AddKeyEvent(modifier, false);
        frame();
        settle();
    }
};

// 单击选中、Ctrl/Shift 仍为单选、双击目录导航、双击场景打开。
bool selectionAndNavigation() {
    Harness ui;
    CHECK(ui.valid);
    ui.settle();

    ui.click(ui.listRow(0)); // Root rows: scenes, shaders (directories first).
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://scenes");
    ui.modClick(ImGuiKey_ModCtrl, ui.listRow(1));
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://shaders");
    // Ctrl/Shift clicks stay single-select: the selection just follows the click.
    ui.modClick(ImGuiKey_ModShift, ui.listRow(0));
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://scenes");

    ui.advance();
    ui.dblclick(ui.listRow(0)); // Enter the scenes folder.
    CHECK(ui.panel.model().currentDirectory().string() == "assets://scenes");
    ui.advance(); // Break the double-click window: without it the next clicks
                  // would count as 3rd/4th (ImGui only fires on count == 2).
    ui.dblclick(ui.listRow(0)); // The scene file is now row 0.
    CHECK(ui.openedScenes.size() == 1 &&
          ui.openedScenes[0].string() == "assets://scenes/demo.scene.json");

    // Back arrow returns to the root.
    const ImVec2 toolbar = ui.toolbarStart();
    const float half = ImGui::GetFrameHeight() * 0.5F;
    ui.click({toolbar.x + half, toolbar.y + half});
    CHECK(ui.panel.model().currentDirectory().string() == "assets://");
    return true;
}

// 搜索框输入过滤右栏内容（跨目录递归匹配）。
bool searchFiltersContent() {
    Harness ui;
    CHECK(ui.valid);
    ui.settle();

    // 定位搜索框：从工具栏向下扫描，点击后输入 "fix" 并检查模型搜索词。
    const ImVec2 toolbar = ui.toolbarStart();
    bool typed = false;
    for (float y = toolbar.y + 10.0F; y < toolbar.y + 90.0F && !typed; y += 5.0F) {
        ui.click({toolbar.x + 90.0F, y});
        ui.type("fix");
        typed = ui.panel.model().searchText() == "fix";
        if (!typed) {
            // 清空误输入（点了别的控件时字符没有进入搜索框，无需清空）。
            ImGui::GetIO().ClearInputCharacters();
        }
    }
    CHECK(typed);
    const std::vector<ProjectEntry> entries = ui.panel.model().contentEntries();
    CHECK(entries.size() == 1);
    CHECK(entries[0].path.string() == "assets://shaders/fixture.shader.json");
    return true;
}

// 右键菜单 Duplicate 与 Rename（就地重命名 + 回车提交）。
bool contextMenuDuplicateAndRename() {
    Harness ui;
    CHECK(ui.valid);
    ui.panel.model().navigate(VirtualPath{"assets://shaders"});
    ui.settle();

    // Row 0 is fixture.shader.json. Menu: Open(0) Reimport(1) Rename(2) Duplicate(3).
    ui.advance();
    ui.click(ui.listRow(0), 1);
    CHECK(ui.menuItem(3));
    CHECK(FILE_SYSTEM.isFile(VirtualPath{"assets://shaders/fixture 1.shader.json"}));

    // After duplicating, "fixture 1..." sorts before "fixture..." (space < '.'),
    // so the original moved to row 1. Typing replaces the pre-filled stem
    // (InputText AutoSelectAll).
    ui.advance();
    ui.click(ui.listRow(1), 1);
    CHECK(ui.menuItem(2));
    ui.type("renamed");
    ui.key(ImGuiKey_Enter);
    CHECK(FILE_SYSTEM.isFile(VirtualPath{"assets://shaders/renamed.shader.json"}));
    CHECK(!FILE_SYSTEM.isFile(VirtualPath{"assets://shaders/fixture.shader.json"}));
    return true;
}

// Delete 键打开确认模态：Cancel 保留，Delete 删除。
// 模态可见性：关闭的 popup 窗口仍留在 g.Windows 里（延迟销毁），只有 Active 标志
// 代表当前打开。
[[nodiscard]] static bool deleteModalVisible() {
    const ImGuiWindow* window = ImGui::FindWindowByName("Confirm Delete");
    return window != nullptr && window->Active;
}

bool deleteModalConfirmAndCancel() {
    Harness ui;
    CHECK(ui.valid);
    ui.panel.model().navigate(VirtualPath{"assets://shaders"});
    ui.settle();

    const VirtualPath fixture{"assets://shaders/fixture.shader.json"};
    ui.click(ui.listRow(0));
    ui.key(ImGuiKey_Delete);
    CHECK(deleteModalVisible());
    const ImGuiWindow* modal = ImGui::FindWindowByName("Confirm Delete");

    // Cancel 在右侧：从右往左扫描模态下半部分，先命中 Cancel。
    bool canceled = false;
    for (float x = modal->Rect().Max.x - 20.0F; x > modal->Rect().Min.x + 10.0F && !canceled;
         x -= 20.0F) {
        for (float y = modal->Rect().Max.y - 60.0F; y < modal->Rect().Max.y - 5.0F && !canceled;
             y += 10.0F) {
            ui.click({x, y});
            canceled = !deleteModalVisible();
        }
    }
    CHECK(canceled);
    CHECK(FILE_SYSTEM.isFile(fixture));

    // Delete 在左侧：从左往右扫描，先命中 Delete。
    ui.click(ui.listRow(0));
    ui.key(ImGuiKey_Delete);
    CHECK(deleteModalVisible());
    modal = ImGui::FindWindowByName("Confirm Delete");
    bool deleted = false;
    for (float x = modal->Rect().Min.x + 20.0F; x < modal->Rect().Max.x - 10.0F && !deleted;
         x += 20.0F) {
        for (float y = modal->Rect().Max.y - 60.0F; y < modal->Rect().Max.y - 5.0F && !deleted;
             y += 10.0F) {
            ui.click({x, y});
            deleted = !FILE_SYSTEM.isFile(fixture);
        }
    }
    CHECK(deleted);
    CHECK(!ASSET_DATABASE.findByPath(fixture).has_value());
    return true;
}

// 拖动条目到目录行触发移动。
bool dragEntryIntoFolder() {
    Harness ui;
    CHECK(ui.valid);
    CHECK(FILE_SYSTEM.writeText(VirtualPath{"assets://notes.txt"}, "notes"));
    ui.settle();

    // Root rows: scenes(0) shaders(1) notes.txt(2) — directories first.
    ui.drag(ui.listRow(2), ui.listRow(0));
    CHECK(FILE_SYSTEM.isFile(VirtualPath{"assets://scenes/notes.txt"}));
    CHECK(!FILE_SYSTEM.isFile(VirtualPath{"assets://notes.txt"}));
    return true;
}

// 网格视图渲染与选择。
bool gridSelection() {
    Harness ui;
    CHECK(ui.valid);
    ui.panel.model().setViewMode(ProjectBrowserModel::ViewMode::Grid);
    ui.settle();

    const ImVec2 start = ui.contentStart();
    const float cellWidth = 96.0F;
    const float pitchY = 76.0F + ImGui::GetStyle().ItemSpacing.y;
    ui.click({start.x + cellWidth * 0.5F, start.y + pitchY * 0.0F + 38.0F});
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://scenes");
    ui.click({start.x + cellWidth * 1.5F, start.y + pitchY * 0.0F + 38.0F});
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://shaders");
    return true;
}

// 目录树叶子行为：无子文件夹的目录不渲染展开箭头（ImGuiTreeNodeFlags_Leaf），
// 点击箭头区域也是直接选中；含子文件夹的目录点击箭头仅切换展开不选中。
bool treeLeafBehavior() {
    Harness ui;
    CHECK(ui.valid);
    CHECK(FILE_SYSTEM.writeText(VirtualPath{"assets://scenes/sub/placeholder.txt"}, "x"));
    ui.settle();

    const ImGuiContext* ctx = ImGui::GetCurrentContext();
    const ImVec2 start = ui.treeStart();
    CHECK(start.x != 0.0F || start.y != 0.0F);

    // y 向扫描收集树行中心（x 固定在初始三行的 label 内）：Assets/scenes/shaders。
    std::vector<float> rows;
    {
        ImGuiID current = 0;
        float rowStart = 0.0F;
        for (float y = 0.0F; y < 150.0F; y += 2.0F) {
            ui.move({start.x + 40.0F, start.y + y});
            const ImGuiID hovered = ctx->HoveredId;
            if (hovered != 0 && current == 0)
                rowStart = y;
            if (hovered == 0 && current != 0)
                rows.push_back(start.y + (rowStart + y) * 0.5F);
            current = hovered;
        }
        if (current != 0)
            rows.push_back(start.y + (rowStart + 148.0F) * 0.5F);
    }
    CHECK(rows.size() == 3); // Assets, scenes (branch), shaders (leaf).

    const float pitch = rows[1] - rows[0];
    const float indent = ImGui::GetStyle().IndentSpacing;
    const float arrow = ImGui::GetTreeNodeToLabelSpacing() * 0.5F;

    // scenes 含子文件夹：点击箭头只展开，不产生选中。
    ui.advance();
    ui.click({start.x + indent + arrow, rows[1]});
    CHECK(ui.panel.model().selectedEntry() == nullptr);

    // 展开后 sub 出现在下一行（缩进两层）；sub 无子文件夹是叶子：点击其
    // 箭头区域直接选中。
    ui.advance();
    ui.click({start.x + 2.0F * indent + arrow, rows[1] + pitch});
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://scenes/sub");

    // shaders 是叶子：点击箭头区域选中而非展开（无 Leaf 标志时会 toggle
    // 并保留旧选中，此断言即回归检测）。scenes 展开后 shaders 下移一行。
    ui.advance();
    ui.click({start.x + indent + arrow, rows[2] + pitch});
    CHECK(ui.panel.model().selectedEntry() &&
          ui.panel.model().selectedEntry()->string() == "assets://shaders");
    return true;
}

} // namespace

int main() {
    return selectionAndNavigation() && searchFiltersContent() && contextMenuDuplicateAndRename() &&
                   deleteModalConfirmAndCancel() && dragEntryIntoFolder() && gridSelection() &&
                   treeLeafBehavior()
               ? 0
               : 1;
}
