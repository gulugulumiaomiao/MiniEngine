#include "asset/importer/ScriptedImporter.h"

#include <algorithm>

namespace engine {

bool sourceExtensionMatches(const VirtualPath& sourcePath, const std::string& extension) {
    if (!sourcePath.valid())
        return false;
    const std::string& path = sourcePath.relativePath();
    if (path.size() < extension.size())
        return false;
    return std::equal(extension.begin(),
                      extension.end(),
                      path.end() - static_cast<std::ptrdiff_t>(extension.size()),
                      [](char expected, char actual) {
                          return expected ==
                                 static_cast<char>(std::tolower(static_cast<unsigned char>(actual)));
                      });
}

bool ScriptedImporter::supports(const VirtualPath& sourcePath) const {
    return sourceExtensionMatches(sourcePath, lowercaseExtension(sourceExtension()));
}

} // namespace engine
