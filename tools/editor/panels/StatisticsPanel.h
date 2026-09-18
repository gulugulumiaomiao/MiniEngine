#pragma once

namespace engine {

namespace editor {

class SceneDocument;

// Scene and frame statistics, independent of the scene image viewport.
class StatisticsPanel {
public:
    explicit StatisticsPanel(SceneDocument& document) : document_(document) {}

    void draw();

private:
    SceneDocument& document_;
};

} // namespace engine::editor
} // namespace engine
