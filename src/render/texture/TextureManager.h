#pragma once

#include "asset/base/AssetId.h"
#include "core/base/HandlePool.h"
#include "core/base/Ref.h"
#include "core/base/Singleton.h"
#include "render/texture/Texture.h"

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace engine {

// Layer-1 Texture cache. The manager indexes resources without owning them:
// callers hold Ref<Texture>, and the last Ref destroys the Texture, which
// unregisters its raw cache entry and notifies TextureStorage.
class TextureResourceManager final : public Singleton<TextureResourceManager> {
public:
    [[nodiscard]] Ref<Texture> load(const AssetId& assetId);
    [[nodiscard]] Ref<Texture> load(const VirtualPath& texturePath);
    [[nodiscard]] Ref<Texture> resolveReference(std::string_view reference);
    [[nodiscard]] Ref<Texture> clone(const Ref<Texture>& source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& texturePath);

    [[nodiscard]] Ref<Texture> defaultWhite();
    [[nodiscard]] Ref<Texture> defaultBlack();
    [[nodiscard]] Ref<Texture> defaultNormal();
    [[nodiscard]] Ref<Texture> errorTexture();
    [[nodiscard]] bool replace(const VirtualPath& texturePath);

    [[nodiscard]] Ref<Texture> find(RID handle) const;
    [[nodiscard]] Ref<Texture> find(const AssetId& assetId) const;
    [[nodiscard]] Ref<Texture> find(const VirtualPath& texturePath) const;
    [[nodiscard]] std::size_t size() const { return resources_.size(); }

    void setDestroyObserver(std::function<void(RID)> observer) {
        destroyObserver_ = std::move(observer);
    }
    void clear();

private:
    friend class Singleton<TextureResourceManager>;
    friend class Texture;
    TextureResourceManager() = default;

    [[nodiscard]] Ref<Texture> loadFromPath(const VirtualPath& path, const AssetId& assetId);
    [[nodiscard]] Ref<Texture> createTexture(VirtualPath path,
                                             AssetId assetId,
                                             TextureDesc desc,
                                             std::vector<TextureMipData> mipData,
                                             std::uint64_t version = 1);
    [[nodiscard]] Ref<Texture> createBuiltin(const VirtualPath& path,
                                             std::uint32_t width,
                                             std::uint32_t height,
                                             std::span<const std::byte> pixels,
                                             TextureColorSpace colorSpace);
    [[nodiscard]] Texture* findRaw(RID handle) const;
    void unregister(Texture* texture);

    mutable HandlePool<Texture*, RID> resources_;
    std::unordered_map<AssetId, RID> assetIndex_;
    std::function<void(RID)> destroyObserver_;
    Ref<Texture> defaultWhite_;
    Ref<Texture> defaultBlack_;
    Ref<Texture> defaultNormal_;
    Ref<Texture> errorTexture_;
};

using TextureManager = TextureResourceManager;

} // namespace engine

#define TEXTURE_RESOURCE_MANAGER (::engine::TextureResourceManager::instance())
#define TEXTURE_MANAGER TEXTURE_RESOURCE_MANAGER
