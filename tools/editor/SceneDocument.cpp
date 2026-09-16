#include "tools/editor/SceneDocument.h"

#include "asset/exporter/AssetExportPipeline.h"
#include "asset/importer/AssetImportPipeline.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "runtime/engine/Engine.h"
#include "scene/scene/Scene.h"
#include "scene/scene/SceneAsset.h"
#include "scene/scene/SceneRuntimeSerializer.h"

namespace engine::editor {

bool SceneDocument::open(const VirtualPath& scenePath) {
    if (!scenePath.valid() || scenePath.scheme() != "assets" ||
        !scenePath.relativePath().ends_with(".scene.json")) {
        Log::error("SceneDocument", "Invalid Scene path: %s", scenePath.string().c_str());
        return false;
    }

    // Engine::loadScene loads the asset, instantiates it and swaps it into the active
    // Scene slot, keeping the previous Scene on failure.
    if (!ENGINE.loadScene(scenePath)) {
        Log::error("SceneDocument", "Cannot open Scene: %s", scenePath.string().c_str());
        return false;
    }

    applyLoaded(scenePath);
    return true;
}

void SceneDocument::createEmpty() {
    ENGINE.resetScene("Untitled Scene");
    ++revision_;
    sourcePath_ = {};
    displayName_ = "Untitled";
    attached_ = true;
    dirty_ = false;
    conflictPending_ = false;
}

bool SceneDocument::save(std::string& error) {
    if (!valid()) {
        error = "no Scene document is open";
        return false;
    }
    if (untitled()) {
        error = "untitled Scene must be saved with an explicit path";
        return false;
    }

    const std::unique_ptr<SceneAsset> asset = exportSceneToAsset(ENGINE.scene(), sourcePath_, error);
    if (!asset || !ASSET_EXPORT_PIPELINE.exportAsset(*asset, sourcePath_, error)) {
        Log::error("SceneDocument", "Cannot save Scene: %s", error.c_str());
        return false;
    }
    if (ASSET_IMPORT_PIPELINE.initialized())
        (void)ASSET_IMPORT_PIPELINE.reimportAsset(sourcePath_);
    ASSET_MANAGER.invalidate(sourcePath_);
    dirty_ = false;
    conflictPending_ = false;
    // The atomic write will be picked up by the file watcher; that notification is
    // self-inflicted and must not reload the freshly saved state.
    suppressNextChange_ = true;
    return true;
}

bool SceneDocument::saveAs(const VirtualPath& scenePath, std::string& error) {
    if (!scenePath.valid() || scenePath.scheme() != "assets" ||
        !scenePath.relativePath().ends_with(".scene.json")) {
        error = "invalid Scene target path: " + scenePath.string();
        return false;
    }
    if (!valid()) {
        error = "no Scene document is open";
        return false;
    }

    sourcePath_ = scenePath;
    const bool saved = save(error);
    if (saved)
        displayName_ = scenePath.filename();
    return saved;
}

void SceneDocument::handleExternalChange(const VirtualPath& path, bool removed) {
    if (!valid() || untitled() || path != sourcePath_)
        return;
    if (suppressNextChange_) {
        suppressNextChange_ = false;
        return;
    }
    if (removed) {
        if (!dirty_)
            Log::warn("SceneDocument", "Scene source was removed: %s", path.string().c_str());
        else
            conflictPending_ = true;
        return;
    }
    if (dirty_) {
        conflictPending_ = true;
        return;
    }
    (void)open(sourcePath_);
}

void SceneDocument::resolveConflict(ConflictPolicy policy) {
    if (!conflictPending_)
        return;
    conflictPending_ = false;
    if (policy == ConflictPolicy::ReloadFromDisk && !untitled())
        (void)open(sourcePath_);
    // KeepLocal: retain the in-memory Scene; the next save overwrites the external file.
}

Scene& SceneDocument::scene() const {
    return ENGINE.scene();
}

void SceneDocument::applyLoaded(VirtualPath path) {
    ++revision_;
    sourcePath_ = std::move(path);
    displayName_ = sourcePath_.filename();
    attached_ = true;
    dirty_ = false;
    conflictPending_ = false;
}

} // namespace engine::editor
