#include "tools/editor/InspectorPanel.h"
#include "tools/editor/SceneDocument.h"
#include "scene/scene/Scene.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>

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

} // namespace

int main() {
    return multiActiveEdit() && multiActiveMixed() && multiTransformEdit() &&
                   multiTransformMixed() && singleAndEmptySmoke()
               ? 0
               : 1;
}
