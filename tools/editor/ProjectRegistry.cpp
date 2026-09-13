#include "tools/editor/ProjectRegistry.h"

#if defined(MINI_EDITOR)

#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/JsonTransfer.h"
#include "core/serialization/Transfer.h"
#include "runtime/config/ProjectConfig.h"

#include <algorithm>

#include <Windows.h>
#include <shellapi.h>

namespace engine::editor {
namespace {

struct SceneMapping : public Transferable {
    std::string project;
    std::string scene;
    SceneMapping() = default;
    SceneMapping(std::string project, std::string scene)
        : project(std::move(project)), scene(std::move(scene)) {}

    bool transfer(Transfer& archive) override {
        return archive.beginObject({}) && archive.transfer("project", project) &&
               archive.transfer("scene", scene) && archive.endObject();
    }
};

std::vector<SceneMapping> mapToVector(
    const std::unordered_map<std::string, std::string>& map) {
    std::vector<SceneMapping> result;
    result.reserve(map.size());
    for (const auto& [key, value] : map)
        result.push_back({key, value});
    return result;
}

std::unordered_map<std::string, std::string> vectorToMap(
    const std::vector<SceneMapping>& vec) {
    std::unordered_map<std::string, std::string> result;
    result.reserve(vec.size());
    for (const SceneMapping& m : vec)
        result[m.project] = m.scene;
    return result;
}

} // namespace

bool RecentProjectEntry::transfer(Transfer& archive) {
    std::string path = rootDirectory.string();
    if (!archive.beginObject({}) || !archive.transfer("root_directory", path))
        return false;
    if (archive.reading())
        rootDirectory = std::filesystem::path{path};
    return archive.transfer("name", name) && archive.endObject();
}

bool ProjectRegistry::transfer(Transfer& archive) {
    if (!archive.beginObject({}) || !archive.transfer("entries", entries_))
        return false;
    if (archive.writing()) {
        std::vector<SceneMapping> mappings = mapToVector(lastScenes_);
        return archive.transfer("last_scenes", mappings) && archive.endObject();
    }
    std::vector<SceneMapping> mappings;
    if (!archive.transfer("last_scenes", mappings))
        return false;
    lastScenes_ = vectorToMap(mappings);
    return archive.endObject();
}

std::optional<ProjectRegistry> ProjectRegistry::load(const VirtualPath& path) {
    const std::optional<std::string> source = FILE_SYSTEM.readText(path);
    if (!source)
        return std::nullopt;
    JsonReader reader{*source};
    if (!reader.valid())
        return std::nullopt;
    ProjectRegistry registry;
    registry.filePath_ = path;
    if (!registry.transfer(reader))
        return std::nullopt;
    registry.pruneInvalid();
    return registry;
}

bool ProjectRegistry::save() const {
    if (!filePath_.valid())
        return false;
    JsonWriter writer;
    ProjectRegistry copy = *this;
    if (!copy.transfer(writer))
        return false;
    const std::string json = writer.toString() + "\n";
    if (!FILE_SYSTEM.writeTextAtomic(filePath_, json)) {
        Log::error("ProjectRegistry", "Cannot write registry to %s", filePath_.string().c_str());
        return false;
    }
    return true;
}

bool ProjectRegistry::addProject(const std::filesystem::path& root, std::string name) {
    const std::filesystem::path normalized =
        std::filesystem::absolute(root).lexically_normal();
    const auto existing = std::ranges::find_if(entries_, [&](const RecentProjectEntry& e) {
        return e.rootDirectory == normalized;
    });
    if (existing != entries_.end()) {
        existing->name = std::move(name);
        std::ranges::rotate(entries_.begin(), existing, existing + 1);
        return true;
    }
    if (entries_.size() >= kMaxEntries)
        return false;
    entries_.insert(entries_.begin(), RecentProjectEntry{normalized, std::move(name)});
    return true;
}

void ProjectRegistry::removeEntry(std::size_t index) {
    if (index >= entries_.size())
        return;
    const std::string key = entries_[index].rootDirectory.string();
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
    lastScenes_.erase(key);
}

void ProjectRegistry::moveToTop(std::size_t index) {
    if (index == 0 || index >= entries_.size())
        return;
    const auto iterator = entries_.begin() + static_cast<std::ptrdiff_t>(index);
    std::ranges::rotate(entries_.begin(), iterator, iterator + 1);
}

void ProjectRegistry::setLastScene(const std::filesystem::path& projectRoot,
                                   std::string scenePath) {
    const std::string key =
        std::filesystem::absolute(projectRoot).lexically_normal().string();
    if (scenePath.empty())
        lastScenes_.erase(key);
    else
        lastScenes_[key] = std::move(scenePath);
}

std::string ProjectRegistry::lastScene(const std::filesystem::path& projectRoot) const {
    const std::string key =
        std::filesystem::absolute(projectRoot).lexically_normal().string();
    const auto found = lastScenes_.find(key);
    return found != lastScenes_.end() ? found->second : std::string{};
}

bool ProjectRegistry::deleteFromDisk(std::size_t index, bool sendToRecycleBin) {
    if (index >= entries_.size())
        return false;
    const std::filesystem::path root = entries_[index].rootDirectory;
    const std::wstring wideRoot = root.wstring();
    std::vector<wchar_t> buffer(wideRoot.begin(), wideRoot.end());
    buffer.push_back(L'\0');
    buffer.push_back(L'\0');

    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = buffer.data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    if (sendToRecycleBin)
        op.fFlags |= FOF_ALLOWUNDO;
    const int result = SHFileOperationW(&op);
    if (result != 0 && result != 1223) {
        Log::error("ProjectRegistry", "SHFileOperationW failed with code %d", result);
        return false;
    }
    if (result == 0)
        removeEntry(index);
    return true;
}

void ProjectRegistry::pruneInvalid() {
    std::erase_if(entries_, [](const RecentProjectEntry& entry) {
        return !isProjectDirectory(entry.rootDirectory);
    });
}

} // namespace engine::editor

#endif // MINI_EDITOR
