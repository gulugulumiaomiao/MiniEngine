#include "render/mesh/MeshManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "render/mesh/MeshBuilder.h"

#include <limits>
#include <utility>
#include <vector>

namespace engine {

Ref<Mesh> MeshResourceManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MeshResourceManager", "Invalid Mesh AssetId");
        return {};
    }
    if (Ref<Mesh> existing = find(assetId))
        return existing;
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("MeshResourceManager", "Unknown Mesh AssetId: %s", assetId.toString().c_str());
        return {};
    }
    return loadFromPath(*path, assetId);
}

Ref<Mesh> MeshResourceManager::load(const VirtualPath& meshPath) {
    if (!meshPath.valid()) {
        Log::error("MeshResourceManager", "Invalid Mesh path: %s", meshPath.string().c_str());
        return {};
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId) {
        Log::error("MeshResourceManager",
                   "Mesh path has no AssetId: %s",
                   meshPath.string().c_str());
        return {};
    }
    if (Ref<Mesh> existing = find(*assetId))
        return existing;
    return loadFromPath(meshPath, *assetId);
}

Ref<Mesh> MeshResourceManager::loadFromPath(const VirtualPath& meshPath, const AssetId& assetId) {
    Log::info("Mesh", "Loading mesh: %s", meshPath.string().c_str());
    const Ref<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    if (!asset)
        return {};
    Ref<Mesh> mesh = asset->instantiate();
    if (!mesh)
        return {};
    mesh->assetId_ = assetId;
    return insert(mesh);
}

Ref<Mesh> MeshResourceManager::clone(const Ref<Mesh>& source) {
    if (!source) {
        Log::error("MeshResourceManager", "Cannot clone an invalid Mesh");
        return {};
    }
    return insertUnkeyed(source->clone());
}

void MeshResourceManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("MeshResourceManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("MeshResourceManager", "Unknown Mesh AssetId: %s", assetId.toString().c_str());
        return;
    }
    const Ref<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(*path);
    Ref<Mesh> mesh = find(assetId);
    if (!asset || !mesh) {
        Log::error("MeshResourceManager", "Failed to reload mesh asset: %s", path->string().c_str());
        return;
    }
    mesh->rebuildFromAsset(*asset);
}

void MeshResourceManager::refreshAsset(const VirtualPath& meshPath) {
    if (!meshPath.valid()) {
        Log::error("MeshResourceManager", "Invalid Mesh path: %s", meshPath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId) {
        Log::error("MeshResourceManager",
                   "Mesh path has no AssetId: %s",
                   meshPath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

Ref<Mesh> MeshResourceManager::createRuntime(const MeshBuildRecipe& recipe) {
    const std::optional<MeshBuildResult> built = MeshBuilder::build(recipe);
    if (!built) {
        Log::error("MeshResourceManager", "Cannot build runtime primitive Mesh: %s", recipe.name.c_str());
        return {};
    }
    return insertUnkeyed(makeRef<Mesh>(built->desc, built->data, recipe));
}

bool MeshResourceManager::rebuildRuntime(const Ref<Mesh>& mesh, const MeshBuildRecipe& recipe) {
    if (!mesh || mesh->isAssetBacked()) {
        Log::error("MeshResourceManager", "Cannot rebuild a non-runtime Mesh");
        return false;
    }
    const std::optional<MeshBuildResult> built = MeshBuilder::build(recipe);
    if (!built) {
        Log::error("MeshResourceManager", "Cannot rebuild runtime primitive Mesh: %s", recipe.name.c_str());
        return false;
    }
    if (mesh->version_ == std::numeric_limits<std::uint64_t>::max())
        Log::fatal("MeshResourceManager", "Mesh version overflow");
    mesh->desc_ = built->desc;
    mesh->data_ = built->data;
    mesh->buildRecipe_ = recipe;
    mesh->cacheVertexLayoutHash();
    ++mesh->version_;
    mesh->dirty_ = true;
    return true;
}

Ref<Mesh> MeshResourceManager::insert(const Ref<Mesh>& mesh) {
    if (!mesh || !validateMesh(mesh->desc(), mesh->data()))
        return {};
    if (mesh->assetId_.valid()) {
        if (Ref<Mesh> existing = find(mesh->assetId_))
            return existing;
    }
    return insertUnkeyed(mesh);
}

Ref<Mesh> MeshResourceManager::insertUnkeyed(const Ref<Mesh>& mesh) {
    if (!mesh || mesh->resourceId_ || !validateMesh(mesh->desc(), mesh->data()))
        return {};
    const RID handle = resources_.insert(mesh.get());
    mesh->resourceId_ = handle;
    if (mesh->assetId_.valid())
        assetIndex_.insert_or_assign(mesh->assetId_, handle);
    return mesh;
}

Mesh* MeshResourceManager::findRaw(RID handle) const {
    Mesh* const* stored = resources_.find(handle);
    return stored ? *stored : nullptr;
}

Ref<Mesh> MeshResourceManager::find(RID handle) const {
    return Ref<Mesh>{findRaw(handle)};
}

Ref<Mesh> MeshResourceManager::find(const AssetId& assetId) const {
    const auto found = assetIndex_.find(assetId);
    return found == assetIndex_.end() ? Ref<Mesh>{} : find(found->second);
}

Ref<Mesh> MeshResourceManager::find(const VirtualPath& meshPath) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    return assetId ? find(*assetId) : Ref<Mesh>{};
}

void MeshResourceManager::unregister(Mesh* mesh) {
    if (!mesh || !mesh->resourceId_)
        return;
    const RID handle = mesh->resourceId_;
    if (mesh->assetId_.valid()) {
        const auto found = assetIndex_.find(mesh->assetId_);
        if (found != assetIndex_.end() && found->second == handle)
            assetIndex_.erase(found);
    }
    if (destroyObserver_)
        destroyObserver_(handle);
    mesh->resourceId_ = {};
    (void)resources_.release(handle);
}

void MeshResourceManager::clear() {
    std::vector<std::pair<RID, Mesh*>> remaining;
    remaining.reserve(resources_.size());
    resources_.forEachHandle(
        [&remaining](RID handle, Mesh* mesh) { remaining.emplace_back(handle, mesh); });
    for (const auto& [handle, mesh] : remaining) {
        if (!findRaw(handle))
            continue;
        if (destroyObserver_)
            destroyObserver_(handle);
        mesh->resourceId_ = {};
        (void)resources_.release(handle);
    }
    assetIndex_.clear();
}

bool MeshResourceManager::replace(const VirtualPath& meshPath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(meshPath);
    if (!assetId)
        return true;
    Ref<Mesh> mesh = find(*assetId);
    if (!mesh)
        return true;
    const Ref<MeshAsset> asset = ASSET_MANAGER.loadAsset<MeshAsset>(meshPath);
    if (!asset)
        return false;
    mesh->rebuildFromAsset(*asset);
    return true;
}

} // namespace engine
