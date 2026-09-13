#pragma once

#include "core/filesystem/VirtualPath.h"

#include <functional>
#include <vector>

namespace engine::editor {

// Unity-style project browser over the assets:// mount. Directories and asset files are
// shown as a tree; .meta files are hidden. Double-clicking a scene opens it.
class ProjectPanel {
public:
    using OpenSceneHandler = std::function<void(const VirtualPath&)>;

    explicit ProjectPanel(OpenSceneHandler openScene) : openScene_(std::move(openScene)) {}

    void draw();

private:
    void drawDirectory(const VirtualPath& directory);

    OpenSceneHandler openScene_;
};

} // namespace engine::editor
