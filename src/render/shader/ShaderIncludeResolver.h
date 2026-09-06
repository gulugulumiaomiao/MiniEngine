#pragma once

#include "core/filesystem/FileSystem.h"

#include <optional>
#include <string_view>

namespace engine {

class ShaderIncludeResolver final {
public:
    [[nodiscard]] static std::optional<VirtualPath> resolve(
        const VirtualPath& includingFile, std::string_view include) {
        if (!includingFile.valid() || include.empty()) return std::nullopt;

        const VirtualPath candidate = include.find("://") != std::string_view::npos
                                          ? VirtualPath{include}
                                          : includingFile.parent().joined(include);
        return candidate.valid() && FILE_SYSTEM.isFile(candidate)
                   ? std::optional<VirtualPath>{candidate}
                   : std::nullopt;
    }
};

} // namespace engine
