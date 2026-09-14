#include "tools/editor/SelectionSet.h"

#include <cstdio>
#include <vector>

namespace {
using engine::editor::SelectionSet;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                           \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

bool basicSelection() {
    const std::vector<int> order{1, 2, 3, 4, 5};
    SelectionSet<int> set;
    CHECK(set.empty() && set.size() == 0);
    // 无修饰点击替换选择。
    set.click(2, false, false, order);
    CHECK(set.size() == 1 && set.contains(2) && set.primary() == 2);
    set.click(4, false, false, order);
    CHECK(set.size() == 1 && set.contains(4) && !set.contains(2));
    set.clear();
    CHECK(set.empty() && !set.contains(4));
    set.select(3);
    CHECK(set.size() == 1 && set.primary() == 3);
    set.remove(3);
    CHECK(set.empty());
    return true;
}

bool ctrlToggles() {
    const std::vector<int> order{1, 2, 3, 4, 5};
    SelectionSet<int> set;
    set.click(2, false, false, order);
    set.click(4, true, false, order);
    CHECK(set.size() == 2 && set.contains(2) && set.contains(4));
    // Ctrl 再点已选中的项将其移除。
    set.click(2, true, false, order);
    CHECK(set.size() == 1 && set.contains(4) && !set.contains(2));
    // Ctrl 加选的项成为新 anchor，后续 Shift 从它开始。
    set.click(4, true, false, order);
    CHECK(set.empty());
    set.click(3, true, false, order);
    set.click(5, false, true, order);
    CHECK(set.size() == 3 && set.contains(3) && set.contains(4) && set.contains(5));
    return true;
}

bool shiftRanges() {
    const std::vector<int> order{10, 20, 30, 40, 50};
    SelectionSet<int> set;
    // 无 anchor 的 Shift 退化为单选并建立 anchor。
    set.click(30, false, true, order);
    CHECK(set.size() == 1 && set.primary() == 30);
    // 向上取范围。
    set.click(10, false, true, order);
    CHECK(set.size() == 3 && set.contains(10) && set.contains(20) && set.contains(30));
    // 连续 Shift 始终基于同一 anchor 收缩/扩张。
    set.click(50, false, true, order);
    CHECK(set.size() == 3 && set.contains(30) && set.contains(40) && set.contains(50));
    // anchor 不在可见列表（折叠）时退化为单选。
    set.click(30, false, false, order);
    set.click(20, false, true, {10, 20});
    CHECK(set.size() == 1 && set.primary() == 20);
    return true;
}

bool removal() {
    SelectionSet<int> set;
    const std::vector<int> order{5, 6, 7};
    set.select(5);
    set.click(6, true, false, order);
    set.click(7, true, false, order);
    CHECK(set.items().size() == 3 && set.primary() == 5);
    // removeIf 去掉失效条目后 anchor（Ctrl 最后加选的 7）仍在集合，Shift 范围正常。
    set.removeIf([](int value) { return value == 6; });
    CHECK(set.size() == 2 && !set.contains(6));
    set.click(5, false, true, {5, 7});
    CHECK(set.size() == 2 && set.contains(5) && set.contains(7));
    // anchor 本身被移除后 Shift 退化为单选。
    set.removeIf([](int value) { return value == 7; });
    CHECK(set.size() == 1);
    set.click(5, false, true, {5});
    CHECK(set.size() == 1 && set.primary() == 5);
    set.removeIf([](int) { return true; });
    CHECK(set.empty());
    return true;
}

} // namespace

int main() {
    return basicSelection() && ctrlToggles() && shiftRanges() && removal() ? 0 : 1;
}
