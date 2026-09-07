#pragma once

#include "core/base/Singleton.h"
#include "core/filesystem/VirtualPath.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine {

enum class FileChangeType { Added, Modified, Removed, Renamed };

struct FileChangeEvent {
    FileChangeType type{FileChangeType::Modified};
    VirtualPath path;
    VirtualPath previousPath;
};

class FileWatcher final : public Singleton<FileWatcher> {
public:
    ~FileWatcher();

    [[nodiscard]] bool start(const VirtualPath& root = VirtualPath{"asset://"},
                             std::chrono::milliseconds debounce = std::chrono::milliseconds{200},
                             bool background = true);
    void stop();
    void scanNow();
    [[nodiscard]] std::vector<FileChangeEvent> pollEvents();
    [[nodiscard]] bool running() const;

private:
    friend class Singleton<FileWatcher>;
    FileWatcher() = default;

    struct SnapshotEntry {
        VirtualPath path;
        std::uintmax_t size{};
        std::filesystem::file_time_type modifiedTime{};
    };
    struct TimedEvent {
        FileChangeEvent event;
        std::chrono::steady_clock::time_point time;
    };

    [[nodiscard]] std::unordered_map<std::string, SnapshotEntry> makeSnapshot() const;
    void enqueue(FileChangeEvent event);
    [[nodiscard]] static bool ignored(const VirtualPath& path);

    VirtualPath root_;
    std::chrono::milliseconds debounce_{200};
    std::unordered_map<std::string, SnapshotEntry> snapshot_;
    std::vector<TimedEvent> queued_;
    std::unordered_map<std::string, TimedEvent> pending_;
    std::jthread worker_;
    mutable std::mutex mutex_;
    bool running_{};
};

} // namespace engine

#define FILE_WATCHER (::engine::FileWatcher::instance())
