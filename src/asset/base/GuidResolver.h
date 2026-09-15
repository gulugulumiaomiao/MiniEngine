#pragma once

#include "asset/base/AssetId.h"
#include "core/filesystem/VirtualPath.h"

#include <optional>

namespace engine {

// Resolves GUIDs to VirtualPaths and vice versa. The canonical implementation
// is AssetDatabase, which derives from this interface.
class GuidResolver {
public:
    virtual ~GuidResolver() = default;

    [[nodiscard]] virtual std::optional<VirtualPath> findPath(const AssetId& guid) const = 0;
    [[nodiscard]] virtual std::optional<AssetId> findGuid(const VirtualPath& path) const = 0;
};

} // namespace engine
