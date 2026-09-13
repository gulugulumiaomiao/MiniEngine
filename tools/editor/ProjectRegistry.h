#pragma once

#if defined(MINI_EDITOR)

#include "core/filesystem/VirtualPath.h"
#include "core/serialization/Transferable.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::editor {

// One entry in the editor's recent project list.
struct RecentProjectEntry : public Transferable {
    std::filesystem::path rootDirectory;
    std::string name;

    RecentProjectEntry() = default;
    RecentProjectEntry(std::filesystem::path rootDirectory, std::string name)
        : rootDirectory(std::move(rootDirectory)), name(std::move(name)) {}

    bool transfer(Transfer& archive) override;
};

// Persists the editor's recent project list and per-project ephemeral state such as
// the last scene that was open. In the editor this registry is embedded in editor.json
// and saved together with the editor config through EditorConfig::save. The standalone
// load/save through a VirtualPath is a separate utility kept for tests and reuse;
// the editor itself never stores the registry in its own file.
class ProjectRegistry : public Transferable {
public:
    static constexpr std::uint32_t kMaxEntries = 16;
    static constexpr std::string_view kDefaultFileName = "editor.json";

    // Loads the registry from a virtual path. Returns nullopt when the file is
    // missing or malformed.
    [[nodiscard]] static std::optional<ProjectRegistry> load(const VirtualPath& path);
    // Writes the registry back to the same virtual path.
    [[nodiscard]] bool save() const;

    void setFilePath(const VirtualPath& path) { filePath_ = path; }
    [[nodiscard]] const VirtualPath& filePath() const { return filePath_; }

    // Adds or refreshes root in the list. When root already exists its name is updated
    // and it moves to the front. Returns false when the list is full and root is new.
    [[nodiscard]] bool addProject(const std::filesystem::path& root, std::string name);
    void removeEntry(std::size_t index);
    void moveToTop(std::size_t index);

    void setLastScene(const std::filesystem::path& projectRoot, std::string scenePath);
    [[nodiscard]] std::string lastScene(const std::filesystem::path& projectRoot) const;

    // Deletes the project directory and removes its entry. When sendToRecycleBin is
    // true the directory is moved to the Windows recycle bin via SHFileOperationW;
    // otherwise it is permanently removed.
    [[nodiscard]] bool deleteFromDisk(std::size_t index, bool sendToRecycleBin);

    // Removes entries whose root directory no longer contains a project.json.
    void pruneInvalid();

    [[nodiscard]] const std::vector<RecentProjectEntry>& entries() const { return entries_; }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    [[nodiscard]] bool empty() const { return entries_.empty(); }

    bool transfer(Transfer& archive) override;

private:

    VirtualPath filePath_;
    std::vector<RecentProjectEntry> entries_;
    std::unordered_map<std::string, std::string> lastScenes_;
};

} // namespace engine::editor

#endif // MINI_EDITOR
