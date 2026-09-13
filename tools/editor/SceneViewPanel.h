#pragma once

namespace engine {

namespace editor {

class SceneDocument;

// Scene-view companion. The engine's swapchain output is the scene view itself and the
// editor panels overlay it, so this panel reports view state and scene statistics
// instead of hosting an off-screen render target.
class SceneViewPanel {
public:
    explicit SceneViewPanel(SceneDocument& document) : document_(document) {}

    void draw();

private:
    SceneDocument& document_;
};

} // namespace engine::editor
} // namespace engine
