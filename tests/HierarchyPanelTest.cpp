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
        for (int i = 0; i < 22; ++i)
            frame();
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

} // namespace

int main() {
    return dragging() && menusAndRename() && hoverAndScroll() && staleDocument() ? 0 : 1;
}
