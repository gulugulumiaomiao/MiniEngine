#include "render/mesh/MeshManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "render/mesh/MeshBuilder.h"

#include <limits>
#include <utility>

namespace engine {

MeshHandle MeshManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MeshManager", "Invalid Mesh AssetId");
        return {};
    }
    if (const MeshHandle existing = findHandle(assetId); existing) {
        return existing;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error(
            "MeshManager", "Unknown Mesh AssetId: %s", assetId.toString().c_str());
        return {};
    }
    return loadFromPath(*path, assetId);
}

MeshHandle MeshManager::load(const VirtualPath& meshPath) {
    if (!meshPath.valid()) {
        Log::error("MeshManager", "Invalid Mesh path: %s", meshPath.string().c_str());
        return {};
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId) {
        Log::error(
            "MeshManager", "Mesh path has no AssetId: %s", meshPath.string().c_str());
        return {};
    }
    if (const MeshHandle existing = findHandle(*assetId); existing) {
        return existing;
    }
    return loadFromPath(meshPath, *assetId);
}

MeshHandle MeshManager::loadFromPath(const VirtualPath& meshPath, const AssetId& assetId) {
    Log::info("Mesh", "Loading mesh: %s", meshPath.string().c_str());
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    if (!asset) {
        return {};
    }
    Mesh mesh = asset->instantiate();
    mesh.assetId_ = assetId;
    return insert(std::move(mesh));
}

MeshHandle MeshManager::clone(MeshHandle source) {
    Mesh* mesh = find(source);
    if (!mesh) {
        Log::error("MeshManager", "Cannot clone an invalid Mesh");
        return {};
    }
    return insertUnkeyed(mesh->clone());
}

void MeshManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MeshManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error(
            "MeshManager", "Unknown Mesh AssetId: %s", assetId.toString().c_str());
        return;
    }
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(*path);
    if (!asset) {
        Log::error("MeshManager", "Failed to reload mesh asset: %s", path->string().c_str());
        return;
    }
    forEach([&assetId, &asset](Mesh& mesh) {
        if (mesh.assetId() == assetId) {
            mesh.rebuildFromAsset(*asset);
        }
    });
}

void MeshManager::refreshAsset(const VirtualPath& meshPath) {
    if (!meshPath.valid()) {
        Log::error("MeshManager", "Invalid Mesh path: %s", meshPath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId) {
        Log::error(
            "MeshManager", "Mesh path has no AssetId: %s", meshPath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

MeshHandle MeshManager::createRuntime(const MeshBuildRecipe& recipe) {
    const std::optional<MeshBuildResult> built = MeshBuilder::build(recipe);
    if (!built) {
        Log::error("MeshManager", "Cannot build runtime primitive Mesh: %s", recipe.name.c_str());
        return {};
    }
    return insertUnkeyed(Mesh{built->desc, built->data, recipe});
}

bool MeshManager::rebuildRuntime(MeshHandle handle, const MeshBuildRecipe& recipe) {
    const Mesh* current = find(handle);
    if (!current || current->isAssetBacked()) {
        Log::error("MeshManager", "Cannot rebuild a non-runtime MeshHandle");
        return false;
    }
    const std::optional<MeshBuildResult> built = MeshBuilder::build(recipe);
    if (!built) {
        Log::error("MeshManager", "Cannot rebuild runtime primitive Mesh: %s", recipe.name.c_str());
        return false;
    }
    return replace(handle, Mesh{built->desc, built->data, recipe});
}

bool MeshManager::destroyRuntime(MeshHandle handle) {
    const Mesh* mesh = find(handle);
    if (!mesh || mesh->isAssetBacked()) {
        Log::error("MeshManager", "Cannot destroy a non-runtime MeshHandle");
        return false;
    }
    return destroy(handle);
}

MeshHandle MeshManager::insert(Mesh mesh) {
    if (!validate(mesh))
        return {};

    const AssetId assetId = mesh.assetId();
    if (assetId.valid()) {
        if (const MeshHandle existing = findHandle(assetId); existing)
            return existing;
    }
    const MeshHandle handle = KeyedHandleRegistry::insert(std::move(mesh));
    return handle;
}

MeshHandle MeshManager::insertUnkeyed(Mesh mesh) {
    if (!validate(mesh))
        return {};
    return KeyedHandleRegistry::insertUnkeyed(std::move(mesh));
}

Mesh* MeshManager::find(const VirtualPath& meshPath) {
    return const_cast<Mesh*>(std::as_const(*this).find(meshPath));
}

const Mesh* MeshManager::find(const VirtualPath& meshPath) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId)
        return nullptr;
    return KeyedHandleRegistry::find(*assetId);
}

MeshHandle MeshManager::findHandle(const VirtualPath& meshPath) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId)
        return {};
    return KeyedHandleRegistry::findHandle(*assetId);
}

bool MeshManager::destroy(MeshHandle handle) {
    Mesh* mesh = find(handle);
    if (!mesh)
        return false;
    if (destroyObserver_)
        destroyObserver_(handle);
    return KeyedHandleRegistry::destroy(handle);
}

void MeshManager::clear() {
    if (destroyObserver_) {
        forEachHandle([this](MeshHandle handle, const Mesh&) { destroyObserver_(handle); });
    }
    KeyedHandleRegistry::clear();
}

bool MeshManager::replace(MeshHandle handle, Mesh mesh) {
    Mesh* current = find(handle);
    if (!current) {
        Log::error("MeshManager", "Cannot replace an invalid MeshHandle");
        return false;
    }
    if (current->assetPath() != mesh.assetPath() || !validate(mesh)) {
        Log::error(
            "MeshManager", "Replacement Mesh is invalid: %s", mesh.assetPath().string().c_str());
        return false;
    }
    if (current->version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::error("MeshManager", "Mesh version overflow");
        return false;
    }
    const AssetId assetId = current->assetId_;
    mesh.version_ = current->version_ + 1;
    mesh.dirty_ = true;
    *current = std::move(mesh);
    current->assetId_ = assetId;
    return true;
}

bool MeshManager::replace(const VirtualPath& meshPath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId) {
        return true;
    }
    const MeshHandle handle = findHandle(*assetId);
    if (!handle) {
        return true;
    }
    const std::shared_ptr<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    if (!asset) {
        return false;
    }
    Mesh* current = find(handle);
    if (!current) {
        return false;
    }
    current->rebuildFromAsset(*asset);
    return true;
}

bool MeshManager::validate(const Mesh& mesh) const {
    return validateMesh(mesh.desc(), mesh.data());
}

} // namespace engine
