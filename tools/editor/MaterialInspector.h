#pragma once

#include "asset/base/Asset.h"
#include "core/filesystem/VirtualPath.h"
#include "render/base/RenderHandle.h"

#include <string>
#include <vector>

namespace engine {

class Material;

namespace editor {

// Unity-style editor for one runtime Material instance. Edits go straight
// through the Material setters, so every MaterialComponent holding the handle
// sees the change the same frame (MATERIAL_MANAGER keeps one instance per
// asset, shared by all users). Asset-backed edits are debounced back into the
// .material.json source followed by a synchronous reimport, which refreshes
// the in-place instance with exactly the written values and makes the later
// FileWatcher event lose the reimport decision instead of overwriting fresher
// edits. The Project asset view and InspectorPanel's MaterialComponent slot
// view reuse this same widget, which is what makes a component-slot edit
// equivalent to editing the asset itself.
class MaterialInspector final {
public:
    // Draws the editor for the material the handle points at. Safe to call
    // every frame with any handle, including invalid ones.
    void draw(MaterialHandle material);

    // Writes any pending debounced edit to disk immediately (target switch,
    // editor shutdown, project close).
    void flushPendingSave();

private:
    void drawIdentity(Material& data);
    void drawShaderCombo(MaterialHandle handle, Material& data);
    void drawRenderQueue(Material& data);
    void drawProperties(Material& data);
    void drawKeywords(Material& data);
    void queueSave(const Material& data);
    void saveNow(const VirtualPath& path);
    // AssetDatabase records of one type, sorted by relative path for stable
    // combo order.
    [[nodiscard]] static std::vector<VirtualPath> collectAssets(AssetType type);

    // The path whose edit is waiting for the debounce window to expire.
    VirtualPath pendingSavePath_{};
    float idleSeconds_{};
    std::string status_;
};

} // namespace editor
} // namespace engine
