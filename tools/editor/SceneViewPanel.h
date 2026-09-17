#pragma once

namespace engine {

namespace editor {

class SceneDocument;

// Displays the scene output and requests its extent from the renderer. The logical
// image ID is resolved to the acquired frame's texture when the overlay is recorded.
class SceneViewPanel {
public:
    explicit SceneViewPanel(SceneDocument& document) : document_(document) {}

    void draw();

private:
    SceneDocument& document_;
};

} // namespace engine::editor
} // namespace engine
