#pragma once

#include "asset/base/AssetId.h"
#include "asset/base/GuidResolver.h"
#include "core/filesystem/VirtualPath.h"

#include <optional>
#include <string>
#include <variant>

namespace engine {

// A cross-asset reference that is either a GUID or a legacy VirtualPath.
// New source files write GUIDs ("guid://<AssetId>"); paths are only used for
// internal tooling and temporary compatibility.
class AssetReference final {
public:
    AssetReference() = default;
    explicit AssetReference(AssetId guid);
    explicit AssetReference(VirtualPath path);
    explicit AssetReference(std::string_view text);

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool isGuid() const;
    [[nodiscard]] bool isPath() const;

    [[nodiscard]] AssetId guid() const;
    [[nodiscard]] const VirtualPath& path() const;

    // Resolves the reference to a VirtualPath. GUID references require a resolver.
    [[nodiscard]] std::optional<VirtualPath> resolve(const GuidResolver& resolver) const;

    [[nodiscard]] std::string toString() const;

private:
    std::variant<std::monostate, AssetId, VirtualPath> value_;
};

} // namespace engine
