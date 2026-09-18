#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace engine::editor {

// One panel's ordered multi-selection. Implements the Unity-style modifier clicks: a
// plain click replaces the selection, Ctrl toggles a single item, Shift selects the
// range between the anchor item and the clicked item over the panel's visible order.
// T only needs default construction and operator==, so both NodeHandle and VirtualPath
// work. The container keeps selection order so panels can show a stable primary item.
template <typename T>
class SelectionSet {
public:
    using Container = std::vector<T>;

    // Applies a click with the current modifier state. orderedItems is the visible
    // order of all selectable rows in the owning panel; it is only consulted for Shift.
    void click(const T& item, bool ctrl, bool shift, const Container& orderedItems) {
        if (shift && hasAnchor_) {
            selectRange(anchor_, item, orderedItems);
            return;
        }
        if (ctrl) {
            if (contains(item)) {
                remove(item);
            } else {
                items_.push_back(item);
                anchor_ = item;
                hasAnchor_ = true;
            }
            return;
        }
        items_.clear();
        items_.push_back(item);
        anchor_ = item;
        hasAnchor_ = true;
    }

    [[nodiscard]] bool contains(const T& item) const {
        return std::ranges::find(items_, item) != items_.end();
    }
    [[nodiscard]] bool empty() const { return items_.empty(); }
    [[nodiscard]] std::size_t size() const { return items_.size(); }
    // Precondition: !empty(). Panels expose the primary through an empty-aware helper.
    [[nodiscard]] const T& primary() const { return items_.front(); }
    [[nodiscard]] const Container& items() const { return items_; }

    void clear() {
        items_.clear();
        hasAnchor_ = false;
    }
    // Replaces the selection with a single item (the panel's select() semantics).
    void select(const T& item) {
        items_.clear();
        items_.push_back(item);
        anchor_ = item;
        hasAnchor_ = true;
    }
    void remove(const T& item) { std::erase_if(items_, [&](const T& other) { return other == item; }); }
    // Drops entries failing the predicate (typically stale handles); a stale anchor
    // makes later Shift clicks fall back to plain selection.
    template <typename Predicate>
    void removeIf(Predicate predicate) {
        std::erase_if(items_, predicate);
        if (hasAnchor_ && !contains(anchor_))
            hasAnchor_ = false;
    }

private:
    void selectRange(const T& anchor, const T& target, const Container& orderedItems) {
        const auto anchorIt = std::ranges::find(orderedItems, anchor);
        const auto targetIt = std::ranges::find(orderedItems, target);
        if (anchorIt == orderedItems.end() || targetIt == orderedItems.end()) {
            select(target); // The anchor is collapsed away; degrade to a plain click.
            return;
        }
        const auto [first, last] = std::minmax(anchorIt, targetIt);
        items_.assign(first, last + 1);
    }

    Container items_;
    T anchor_{};
    bool hasAnchor_{};
};

} // namespace engine::editor
