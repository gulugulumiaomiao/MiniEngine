#include "tools/editor/HierarchyPanel.h"
#include "tools/editor/SceneDocument.h"
#include "scene/scene/Scene.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <string>

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

// 直接向 ImGui 注入输入事件，不创建窗口、GPU 或用户磁盘项目。
struct Harness {
    SceneDocument document;
    HierarchyPanel panel{document};

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
        panel.draw();
        ImGui::Render();
    }
    void settle() {
        frame();
        frame();
    }
    // 推进模拟时间越过双击窗口，防止连续同位置的单击被合并成双击。
    void advance(int frames = 22) {
        for (int i = 0; i < frames; ++i)
            frame();
    }
    ImVec2 row(int index, float fraction = 0.5F) const {
        const ImGuiWindow* window = ImGui::FindWindowByName("Hierarchy");
        return {100,
                window->DC.CursorStartPos.y +
                    static_cast<float>(index) * ImGui::GetTextLineHeightWithSpacing() +
                    fraction * ImGui::GetTextLineHeight()};
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
    void drag(ImVec2 from, ImVec2 to) {
        // 不把连续拖动起点的单击误合并成双击重命名。
        advance();
        move(from);
        button(0, true);
        move({from.x + 10, from.y});
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
    void key(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        frame();
    }
    void modClick(ImGuiKey modifier, ImVec2 position, int mouseButton = 0) {
        // 按下修饰键完成一次点击再释放，模拟 Ctrl/Shift+点击。
        move(position);
        ImGui::GetIO().AddKeyEvent(modifier, true);
        frame();
        button(mouseButton, true);
        button(mouseButton, false);
        ImGui::GetIO().AddKeyEvent(modifier, false);
        frame();
        settle();
    }
};

bool dragging() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto root = scene.rootHandle();
    const auto a = scene.createNode("A"), b = scene.createNode("B"), c = scene.createNode("C");
    scene.findNode(a)->transform().setLocalPosition({5, 0, 0});
    const auto before = scene.findNode(b)->transform().worldPosition();
    ui.settle();
    ui.drag(ui.row(2), ui.row(1));
    CHECK(scene.findNode(b)->parent() == a);
    CHECK(scene.findNode(b)->transform().worldPosition() == before);
    CHECK(ui.document.dirty() && ui.panel.selection() == b);
    // A 已展开，C 位于第四行；插到 A 之前后根层顺序必须变为 C、A。
    ui.drag(ui.row(3), ui.row(1, 0.1F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{c, a}));
    ui.drag(ui.row(3), {100, 400});
    CHECK(scene.findNode(b)->parent() == root);
    CHECK((scene.root().children() == std::vector<NodeHandle>{c, a, b}));
    ui.drag(ui.row(1), ui.row(3, 0.9F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, b, c}));
    // 拖入自己和后代都不得改变树。
    ui.drag(ui.row(2), ui.row(1));
    CHECK(scene.findNode(b)->parent() == a);
    const auto order = scene.root().children();
    ui.drag(ui.row(1), ui.row(2));
    CHECK(scene.root().children() == order && scene.findNode(a)->parent() == root);
    return true;
}

bool menusAndRename() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A");
    ui.settle();
    ui.click(ui.row(1), 1);
    CHECK(ui.panel.selection() == a);
    CHECK(ui.menuItem(0));
    CHECK(scene.findNode(a)->children().size() == 1 && ui.document.dirty());
    const auto child = scene.findNode(a)->children().front();
    CHECK(ui.panel.selection() == child);
    ui.key(ImGuiKey_Escape);
    CHECK(scene.findNode(child)->name() == "Node");
    // 长名称不能溢出，也不能在 Escape 时被隐式提交。
    const std::string longName(600, 'L');
    scene.findNode(child)->setName(longName);
    ui.click(ui.row(2), 1);
    CHECK(ui.menuItem(1));
    ImGui::GetIO().AddInputCharactersUTF8("Cancelled");
    ui.frame();
    ui.key(ImGuiKey_Escape);
    CHECK(scene.findNode(child)->name() == longName);
    ui.click(ui.row(2), 1);
    CHECK(ui.menuItem(1));
    ImGui::GetIO().AddInputCharactersUTF8("Renamed");
    ui.frame();
    ui.key(ImGuiKey_Enter);
    CHECK(scene.findNode(child)->name() == "Renamed");
    // 删除父节点时整个子树一并删除，选择回到 Scene Root。
    ui.click(ui.row(1), 1);
    CHECK(ui.menuItem(2));
    CHECK(!scene.findNode(a) && !scene.findNode(child));
    CHECK(ui.panel.selection() == scene.rootHandle());
    ui.click({100, 400}, 1);
    CHECK(ui.menuItem(0));
    CHECK(scene.root().children().size() == 1);
    ui.key(ImGuiKey_Escape);
    ui.click({100, 400});
    CHECK(!ui.panel.selection());
    return true;
}

bool hoverAndScroll() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto parent = scene.createNode("Same name"), child = scene.createNode("Same name");
    const auto source = scene.createNode("Same name");
    CHECK(scene.findNode(child)->setParent(parent));
    ui.settle();
    // 折叠节点中央悬停自动展开，即使所有节点同名也不会串用身份。
    ui.move(ui.row(2));
    ui.button(0, true);
    ui.move({115, ui.row(2).y});
    ui.move(ui.row(1));
    for (int i = 0; i < 45; ++i)
        ui.frame();
    ui.button(0, false);
    ui.settle();
    CHECK((scene.findNode(parent)->children() == std::vector<NodeHandle>{child, source}));
    ui.click(ui.row(2));
    CHECK(ui.panel.selection() == child);
    // 大树的滚动只跟随拖动方向，不需要离开 Hierarchy。
    for (int i = 0; i < 80; ++i)
        (void)scene.createNode("Scroll item");
    for (int i = 0; i < 22; ++i)
        ui.frame();
    ui.move(ui.row(2));
    ui.button(0, true);
    ui.move({115, ui.row(2).y});
    ui.move({100, 490});
    for (int i = 0; i < 35; ++i)
        ui.frame();
    CHECK(ImGui::FindWindowByName("Hierarchy")->Scroll.y > 0.0F);
    const float scroll = ImGui::FindWindowByName("Hierarchy")->Scroll.y;
    ui.move({100, 25});
    for (int i = 0; i < 15; ++i)
        ui.frame();
    CHECK(ImGui::FindWindowByName("Hierarchy")->Scroll.y < scroll);
    ui.move({600, 550});
    ui.button(0, false);
    return true;
}

bool staleDocument() {
    Harness ui;
    const auto oldNode = ui.document.scene().createNode("Old");
    ui.settle();
    ui.move(ui.row(1));
    ui.button(0, true);
    ui.move({115, ui.row(1).y});
    CHECK(ImGui::GetDragDropPayload());
    const auto revision = ui.document.revision();
    ui.document.createEmpty();
    const auto newNode = ui.document.scene().createNode("New");
    const auto target = ui.document.scene().createNode("Target");
    CHECK(ui.document.revision() != revision && newNode == oldNode);
    ui.panel.syncDocument();
    CHECK(!ui.panel.selection());
    ui.move(ui.row(2));
    ui.button(0, false);
    ui.settle();
    CHECK(ui.document.scene().findNode(newNode)->parent() == ui.document.scene().rootHandle());
    CHECK(ui.document.scene().findNode(target)->children().empty() && !ui.document.dirty());
    ui.click(ui.row(1), 1);
    CHECK(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
    ui.document.createEmpty();
    ui.panel.syncDocument();
    ui.settle();
    CHECK(ImGui::GetCurrentContext()->OpenPopupStack.empty());
    return true;
}

bool multiSelection() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A"), b = scene.createNode("B"),
               c = scene.createNode("C"), d = scene.createNode("D");
    ui.settle();
    // Ctrl 加选；selection() 兼容视图指向 primary（首个选中项）。
    ui.click(ui.row(1));
    CHECK(ui.panel.selection() == a && ui.panel.selectionSet().size() == 1);
    ui.modClick(ImGuiKey_ModCtrl, ui.row(3));
    CHECK(ui.panel.selectionSet().size() == 2);
    CHECK(ui.panel.selectionSet().contains(a) && ui.panel.selectionSet().contains(c));
    CHECK(ui.panel.selection() == a);
    // Ctrl 点击已选项把它移出选择。
    ui.modClick(ImGuiKey_ModCtrl, ui.row(1));
    CHECK(ui.panel.selectionSet().size() == 1 && !ui.panel.selectionSet().contains(a));
    CHECK(ui.panel.selection() == c);
    // Shift 从 anchor 起选择连续范围。
    ui.click(ui.row(2));
    ui.modClick(ImGuiKey_ModShift, ui.row(4));
    CHECK(ui.panel.selectionSet().size() == 3);
    CHECK(ui.panel.selectionSet().contains(b) && ui.panel.selectionSet().contains(c) &&
          ui.panel.selectionSet().contains(d));
    // 无修饰点击替换为单选。
    ui.click(ui.row(1));
    CHECK(ui.panel.selectionSet().size() == 1 && ui.panel.selection() == a);
    // Ctrl+双击只影响选择，不进入重命名。
    ui.modClick(ImGuiKey_ModCtrl, ui.row(2));
    ui.modClick(ImGuiKey_ModCtrl, ui.row(2));
    CHECK(scene.findNode(b)->name() == "B");
    CHECK(ui.panel.selectionSet().size() == 1 && ui.panel.selection() == a);
    // 右键组外节点恢复单选，菜单回到单项形态。
    ui.click(ui.row(3), 1);
    CHECK(ui.panel.selectionSet().size() == 1 && ui.panel.selection() == c);
    CHECK(ui.menuItem(0));
    CHECK(scene.findNode(c)->children().size() == 1);
    ui.key(ImGuiKey_Escape);
    // 多选右键菜单：批量激活/停用整组。
    ui.click(ui.row(1));
    ui.modClick(ImGuiKey_ModCtrl, ui.row(2));
    CHECK(ui.panel.selectionSet().size() == 2);
    ui.click(ui.row(1), 1);
    CHECK(ui.panel.selectionSet().size() == 2);
    CHECK(ui.menuItem(1));
    CHECK(scene.findNode(a)->activeSelf() && scene.findNode(b)->activeSelf());
    CHECK(ui.document.dirty());
    ui.click(ui.row(2), 1);
    CHECK(ui.menuItem(2));
    CHECK(!scene.findNode(a)->activeSelf() && !scene.findNode(b)->activeSelf());
    // 多选删除：整组消失，全删光时选择回到 Scene Root。
    ui.click(ui.row(1), 1);
    CHECK(ui.menuItem(0));
    CHECK(!scene.findNode(a) && !scene.findNode(b));
    CHECK(ui.panel.selection() == scene.rootHandle());
    return true;
}

bool multiDrag() {
    Harness ui;
    auto& scene = ui.document.scene();
    const auto a = scene.createNode("A"), b = scene.createNode("B"),
               c = scene.createNode("C"), d = scene.createNode("D");
    ui.settle();
    // Ctrl 选 A、B 后拖 A：整组随行落在 D 之后，拖动后选择保持整组。
    ui.click(ui.row(1));
    ui.modClick(ImGuiKey_ModCtrl, ui.row(2));
    CHECK(ui.panel.selectionSet().size() == 2);
    ui.drag(ui.row(1), ui.row(4, 0.9F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{c, d, a, b}));
    CHECK(ui.panel.selectionSet().size() == 2 && ui.panel.selectionSet().contains(a));
    // 选择仍为 {A,B}，再拖 A 到 D 中央：整组成为 D 的子节点并保持相对顺序。
    ui.drag(ui.row(3), ui.row(2, 0.5F));
    CHECK((scene.findNode(d)->children() == std::vector<NodeHandle>{a, b}));
    CHECK((scene.root().children() == std::vector<NodeHandle>{c, d}));
    // 点击箭头展开 D（会清掉选择），重新组选后拖回根层顶部：跨父级批量且世界位置不变。
    ui.click({15, ui.row(2).y});
    ui.click(ui.row(3));
    ui.modClick(ImGuiKey_ModCtrl, ui.row(4));
    scene.findNode(b)->transform().setLocalPosition({3, 0, 0});
    const auto worldBefore = scene.findNode(b)->transform().worldPosition();
    ui.drag(ui.row(3), ui.row(1, 0.1F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, b, c, d}));
    CHECK(scene.findNode(d)->children().empty());
    CHECK(scene.findNode(b)->transform().worldPosition() == worldBefore);
    // 目标是拖动集合成员（Before B / Into B）时整组拒绝，树不变。
    ui.drag(ui.row(1), ui.row(2, 0.1F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, b, c, d}));
    ui.drag(ui.row(1), ui.row(2, 0.5F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, b, c, d}));
    // Shift 范围选择后从组内拖 B 到 D 之后：整组按可见顺序落在 D 后。
    // 与上一拖动起点同位置的快速单击会构成双击进入重命名（重命名框占据额外一行，
    // 后续行整体下移），先推进时间打破双击窗口。
    ui.advance();
    ui.click(ui.row(1));
    ui.modClick(ImGuiKey_ModShift, ui.row(3));
    CHECK(ui.panel.selectionSet().size() == 3);
    ui.drag(ui.row(2), ui.row(4, 0.9F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{d, a, b, c}));
    // 无修饰点击组外节点后拖动只携带该节点（单拖回归）。
    ui.click(ui.row(1));
    CHECK(ui.panel.selectionSet().size() == 1);
    ui.drag(ui.row(1), ui.row(3, 0.1F));
    CHECK((scene.root().children() == std::vector<NodeHandle>{a, d, b, c}));
    CHECK(ui.panel.selectionSet().size() == 1 && ui.panel.selection() == d);
    return true;
}

} // namespace

int main() {
    return dragging() && menusAndRename() && hoverAndScroll() && staleDocument() && multiSelection() &&
                   multiDrag()
               ? 0
               : 1;
}
