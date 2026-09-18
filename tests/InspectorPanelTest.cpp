#include "tools/editor/panels/InspectorPanel.h"
#include "tools/editor/model/SceneDocument.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "scene/components/MaterialComponent.h"
#include "scene/scene/Scene.h"
#include "core/filesystem/FileWatcher.h"
#include "TestAssetEnvironment.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
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

// 资产视图用例的 fixture：Metallic 排在 BaseColor 之前，扫描点击先命中 slider。
constexpr std::string_view kVertexShader = "#version 450\nvoid main(){gl_Position=vec4(0);}\n";
constexpr std::string_view kFragmentShader =
    "#version 450\nlayout(location=0) out vec4 c;void main(){c=vec4(1);}\n";
constexpr std::string_view kFixtureShaderJson = R"json({
  "$schemaVersion": 1,
  "name": "PanelShader",
  "properties": [
    { "name": "Metallic", "type": "Range", "default": 0.0, "range": [0.0, 1.0] },
    { "name": "BaseColor", "type": "Color", "default": [1, 1, 1, 1] }
  ],
  "subShaders": [{
    "passes": [{
      "name": "Forward",
      "features": ["RECEIVE_SHADOWS"],
      "program": { "vertex": "panel.vert", "frag": "panel.frag" }
    }]
  }]
})json";
constexpr std::string_view kWarmMaterialJson = R"json({
  "$schemaVersion": 1,
  "name": "Warm",
  "shader": "assets://shaders/fixture.shader.json",
  "properties": {
    "BaseColor": [1.0, 0.5, 0.2, 1.0]
  }
})json";
const VirtualPath kWarmMaterialPath{"assets://materials/warm.material.json"};

// 直接向 ImGui 注入输入事件驱动 Inspector 的多选视图，不创建窗口或 GPU。控件没有
// 稳定的行号可寻址，测试用带副作用的扫描点击/拖动来定位交互控件。
struct Harness {
    SceneDocument document;
    InspectorPanel panel{document};
    SelectionSet<NodeHandle> selection;

    Harness() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.0F / 60.0F;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        document.createEmpty();
    }
    ~Harness() { ImGui::DestroyContext(); }

    void frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({400, 500});
        panel.draw(selection);
        ImGui::Render();
    }
    void settle() {
        frame();
        frame();
    }
    void move(ImVec2 position) {
        ImGui::GetIO().AddMousePosEvent(position.x, position.y);
        frame();
    }
    void button(int button, bool down) {
        ImGui::GetIO().AddMouseButtonEvent(button, down);
        frame();
    }
    void click(ImVec2 position) {
        move(position);
        button(0, true);
        button(0, false);
        settle();
    }
    void drag(ImVec2 from, ImVec2 to) {
        move(from);
        button(0, true);
        move(to);
        settle();
        button(0, false);
        settle();
    }
    // press 落在目标、release 移到窗口空白：Checkbox/Button/Selectable 不触发（它们
    // 由 release 激活），Combo 只展开不选中，Slider 在 press 时跳值并随拖动 clamp 到
    // 轨道右端——扫描副作用可控。
    void pressClick(ImVec2 position) {
        move(position);
        button(0, true);
        move({390.0F, 480.0F});
        button(0, false);
        settle();
    }
    // Inspector 内容区坐标；CursorStartPos 在帧间保留，与 HierarchyPanelTest 的做法一致。
    ImVec2 content(float x, float y) const {
        const ImGuiWindow* window = ImGui::FindWindowByName("Inspector");
        return {window->DC.CursorStartPos.x + x, window->DC.CursorStartPos.y + y};
    }
};

// 一致的激活态：点击复选框把整组一起翻转。
bool multiActiveEdit() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A"), b = scene.createNode("B");
    ui.selection.select(a);
    ui.selection.click(b, true, false, {a, b});
    ui.settle();
    // Active 复选框位于文本行与分隔线之后，从上向下扫描直到激活态翻转。
    for (float y = 0.0F; y < 90.0F; y += 5.0F) {
        const bool before = scene.findNode(a)->activeSelf();
        ui.click(ui.content(10.0F, y));
        if (scene.findNode(a)->activeSelf() != before)
            break;
    }
    CHECK(!scene.findNode(a)->activeSelf() && !scene.findNode(b)->activeSelf());
    CHECK(ui.document.dirty());
    return true;
}

// 混合激活态：复选框渲染三态方块，首次点击把整组统一为激活。
bool multiActiveMixed() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A"), b = scene.createNode("B");
    scene.findNode(b)->setActive(false);
    ui.selection.select(a);
    ui.selection.click(b, true, false, {a, b});
    ui.settle();
    for (float y = 0.0F; y < 90.0F; y += 5.0F) {
        ui.click(ui.content(10.0F, y));
        if (scene.findNode(b)->activeSelf())
            break;
    }
    CHECK(scene.findNode(a)->activeSelf() && scene.findNode(b)->activeSelf());
    CHECK(ui.document.dirty());
    return true;
}

// 一致的 Transform：拖动 Position 同步写入所有选中节点。
bool multiTransformEdit() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A"), b = scene.createNode("B");
    ui.selection.select(a);
    ui.selection.click(b, true, false, {a, b});
    ui.settle();
    // 倒序扫描（下方空白/Scale/Rotation 先于 Position），避免先命中折叠头。
    bool dragged = false;
    for (float y = 280.0F; y > 40.0F; y -= 6.0F) {
        const auto before = scene.findNode(a)->transform().localPosition();
        ui.drag(ui.content(50.0F, y), ui.content(70.0F, y));
        if (scene.findNode(a)->transform().localPosition() != before) {
            dragged = true;
            break;
        }
    }
    CHECK(dragged);
    CHECK(scene.findNode(a)->transform().localPosition() ==
          scene.findNode(b)->transform().localPosition());
    CHECK(scene.findNode(a)->transform().localPosition().x > 0.0F);
    return true;
}

// 混合 Transform：Position 显示灰色占位，拖动扫描不改动任何位置。
bool multiTransformMixed() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A"), b = scene.createNode("B");
    scene.findNode(b)->transform().setLocalPosition({5, 0, 0});
    ui.selection.select(a);
    ui.selection.click(b, true, false, {a, b});
    ui.settle();
    for (float y = 280.0F; y > 40.0F; y -= 6.0F)
        ui.drag(ui.content(50.0F, y), ui.content(70.0F, y));
    CHECK(scene.findNode(a)->transform().localPosition() == math::Vec3(0.0F, 0.0F, 0.0F));
    CHECK(scene.findNode(b)->transform().localPosition() == math::Vec3(5.0F, 0.0F, 0.0F));
    return true;
}

// 空选择与单选（完整节点视图）的冒烟。
bool singleAndEmptySmoke() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A");
    ui.settle();
    ui.selection.select(a);
    ui.settle();
    scene.findNode(a)->setName("Renamed");
    ui.settle();
    ui.selection.clear();
    ui.settle();
    CHECK(scene.findNode(a)->name() == "Renamed");
    return true;
}

// 资产视图用例的临时资产环境：物理写入 fixture 后一次性导入（AssetDatabase 里有
// GUID），FileWatcher 停用隔离异步重导入。声明顺序保证 Harness（ImGui context）
// 先于环境销毁。
struct AssetScope {
    std::filesystem::path root;
    bool valid{};

    AssetScope() {
        root = std::filesystem::temp_directory_path() /
               ("MiniEngineInspectorAssetTest-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path assets = root / "assets";
        const auto write = [&assets](const char* relative, std::string_view content) {
            const std::filesystem::path target = assets / relative;
            std::error_code ignored;
            std::filesystem::create_directories(target.parent_path(), ignored);
            std::ofstream stream(target, std::ios::binary);
            stream << std::string{content};
            return stream.good();
        };
        valid = write("shaders/panel.vert", kVertexShader) &&
                write("shaders/panel.frag", kFragmentShader) &&
                write("shaders/fixture.shader.json", kFixtureShaderJson) &&
                write("materials/warm.material.json", kWarmMaterialJson) &&
                test::initializeAssetEnvironment(assets);
        if (valid)
            FILE_WATCHER.stop();
    }
    ~AssetScope() {
        FILE_WATCHER.stop();
        test::shutdownAssetEnvironment();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

// Project 选中材质 → 空层级下 Inspector 显示资产视图，控件编辑直达实例。
bool inspectAssetShowsMaterialEditor() {
    AssetScope assets;
    CHECK(assets.valid);
    Harness ui;
    const MaterialHandle handle = MATERIAL_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    CHECK(MATERIAL_MANAGER.find(handle)->getFloat("Metallic") == 0.0F);

    ui.panel.inspectAsset(kWarmMaterialPath);
    ui.settle();
    bool edited = false;
    for (float y = 0.0F; y < 240.0F && !edited; y += 6.0F) {
        ui.pressClick(ui.content(100.0F, y));
        if (MATERIAL_MANAGER.find(handle)->getFloat("Metallic") != 0.0F)
            edited = true;
    }
    CHECK(edited);
    CHECK(MATERIAL_MANAGER.find(handle)->getFloat("Metallic") == 1.0F);
    return true;
}

// 最后交互优先（模拟 EditorApplication 的戳仲裁序列）：Project 点资产 → 资产
// 视图；Hierarchy 点节点 → 节点视图夺回（资产仍选中也不可编辑）；Project 重复
// 点同一资产 → 值未变也夺回；Project 侧取消选择 → 回节点视图。
bool lastInteractionWins() {
    AssetScope assets;
    CHECK(assets.valid);
    Harness ui;
    auto& scene = ui.document.scene();
    const auto node = scene.createNode("A");
    const MaterialHandle handle = MATERIAL_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    auto canEditMetallic = [&]() {
        bool edited = false;
        for (float y = 0.0F; y < 240.0F && !edited; y += 6.0F) {
            ui.pressClick(ui.content(100.0F, y));
            if (MATERIAL_MANAGER.find(handle)->getFloat("Metallic") != 0.0F)
                edited = true;
        }
        return edited;
    };
    auto material = [&]() { return MATERIAL_MANAGER.find(handle); };

    ui.panel.inspectAsset(kWarmMaterialPath);
    ui.settle();
    CHECK(canEditMetallic());
    material()->setFloat("Metallic", 0.0F); // 重置运行时值，供下一阶段探测

    ui.panel.focusNodeSelection();
    ui.selection.select(node);
    ui.settle();
    CHECK(!canEditMetallic());

    ui.panel.inspectAsset(kWarmMaterialPath); // 重复点同一资产也要夺回面板
    ui.settle();
    CHECK(canEditMetallic());
    material()->setFloat("Metallic", 0.0F);

    ui.panel.clearAssetInspection(); // Project 侧取消选择：节点选择仍在
    ui.settle();
    CHECK(!canEditMetallic());

    ui.panel.focusNodeSelection(); // Hierarchy 点空白取消：视图清空
    ui.selection.clear();
    ui.settle();
    CHECK(!canEditMetallic());
    return true;
}

// MaterialComponent 槽位内嵌同一 MaterialInspector：组件视图里的编辑同样生效。
bool componentSlotEmbedsEditor() {
    AssetScope assets;
    CHECK(assets.valid);
    Harness ui;
    auto& scene = ui.document.scene();
    const auto node = scene.createNode("A");
    const MaterialHandle handle = MATERIAL_MANAGER.load(kWarmMaterialPath);
    CHECK(static_cast<bool>(handle));
    MaterialComponent* component = scene.findNode(node)->addComponent<MaterialComponent>();
    component->setMaterial(0, handle);
    ui.selection.select(node);
    ui.settle();

    bool edited = false;
    for (float y = 0.0F; y < 420.0F && !edited; y += 6.0F) {
        ui.pressClick(ui.content(100.0F, y));
        if (MATERIAL_MANAGER.find(handle)->getFloat("Metallic") != 0.0F)
            edited = true;
    }
    CHECK(edited);
    CHECK(component->material(0) == handle);
    return true;
}

} // namespace

int main() {
    return multiActiveEdit() && multiActiveMixed() && multiTransformEdit() &&
                   multiTransformMixed() && singleAndEmptySmoke() &&
                   inspectAssetShowsMaterialEditor() && lastInteractionWins() &&
                   componentSlotEmbedsEditor()
               ? 0
               : 1;
}
