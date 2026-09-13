#pragma once

#include "core/filesystem/VirtualPath.h"
#include "scene/node/SceneHandles.h"

#include <string>
#include <vector>

namespace engine {

class MeshComponent;
class Node;

namespace editor {

class SceneDocument;

// Inspector for the selected node: name, active flag and per-component editors for
// Transform, Mesh, Material, Camera and Light using the existing runtime API.
class InspectorPanel {
public:
    explicit InspectorPanel(SceneDocument& document) : document_(document) {}

    void draw(NodeHandle selection);

private:
    void drawNodeHeader(Node& node);
    void drawTransform(Node& node);
    void drawMesh(Node& node);
    void drawMaterial(Node& node);
    void drawCamera(Node& node);
    void drawLight(Node& node);
    void drawAddComponent(Node& node);
    void drawPrimitive(MeshComponent& mesh);

    // Lists assets:// files of one extension (without .meta) for combo boxes.
    [[nodiscard]] std::vector<VirtualPath> listAssetFiles(const char* extension) const;

    SceneDocument& document_;
    std::string statusMessage_;
};

} // namespace editor
} // namespace engine
