#pragma once

#include "core/filesystem/VirtualPath.h"

#include <cstdint>
#include <string>

namespace engine {

class Scene;

namespace editor {

// Editing state of the scene document currently open in the editor. Owns nothing;
// the runtime Scene stays owned by the Engine (which may replace it on load or
// reload), so scene access always goes through ENGINE.scene().
class SceneDocument {
public:
    enum class ConflictPolicy {
        // Reload from disk, discarding in-memory changes.
        ReloadFromDisk,
        // Keep the in-memory Scene and stay dirty; saving overwrites the external change.
        KeepLocal,
    };

    // Opens scenePath through Engine::loadScene and swaps it into the active scene
    // slot. Returns false when load or instantiation failed (the previous document,
    // if any, stays untouched).
    [[nodiscard]] bool open(const VirtualPath& scenePath);

    // Starts an untitled empty document. The scene has no source path until saved.
    void createEmpty();

    // Writes the current runtime Scene back to sourcePath() and reimports it. Keeps the
    // document dirty on failure and stores the reason in error.
    [[nodiscard]] bool save(std::string& error);
    // Same, but first switches the document to a new source path (Save As).
    [[nodiscard]] bool saveAs(const VirtualPath& scenePath, std::string& error);

    // Handles an external change notification for the active scene path. Clean documents
    // are reloaded automatically; dirty documents record the conflict.
    void handleExternalChange(const VirtualPath& path, bool removed);

    // Resolves a pending external-change conflict according to policy.
    void resolveConflict(ConflictPolicy policy);

    // Marks the document as modified; called after any editor mutation.
    void markDirty() { dirty_ = true; }

    // 场景替换时递增，用于丢弃旧场景的选择和拖放请求；保存不改变代次。
    [[nodiscard]] std::uint64_t revision() const { return revision_; }
    [[nodiscard]] bool valid() const { return attached_; }
    [[nodiscard]] Scene& scene() const;
    [[nodiscard]] const VirtualPath& sourcePath() const { return sourcePath_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    [[nodiscard]] bool untitled() const { return !sourcePath_.valid(); }
    [[nodiscard]] bool conflictPending() const { return conflictPending_; }
    [[nodiscard]] const std::string& displayName() const { return displayName_; }

private:
    void applyLoaded(VirtualPath path);

    std::uint64_t revision_{};
    VirtualPath sourcePath_;
    std::string displayName_{"Untitled"};
    bool attached_{};
    bool dirty_{};
    bool conflictPending_{};
    // The file watcher reports the editor's own atomic write as an external change;
    // the first matching notification after a save is ignored.
    bool suppressNextChange_{};
};

} // namespace editor
} // namespace engine
