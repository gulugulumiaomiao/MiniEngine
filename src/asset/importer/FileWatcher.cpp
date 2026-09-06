#include "asset/importer/FileWatcher.h"

#include "core/logging/Log.h"
#include "core/filesystem/FileSystem.h"

#include <algorithm>
#include <thread>

namespace engine {

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::ignored(const VirtualPath& path) {
    const std::string name = path.filename();
    return name.empty() || name.starts_with(".") || name.ends_with("~") ||
           name.ends_with(".tmp") || name.ends_with(".swp") ||
           name.ends_with(".part") || name.find(".tmp-") != std::string::npos;
}

std::unordered_map<std::string, FileWatcher::SnapshotEntry>
FileWatcher::makeSnapshot() const {
    std::unordered_map<std::string, SnapshotEntry> result;
    for (const VirtualPath& path : FILE_SYSTEM.listFiles(root_, true)) {
        if (ignored(path)) continue;
        const auto stat = FILE_SYSTEM.stat(path);
        if (stat && stat->isFile) {
            result.emplace(path.string(),
                           SnapshotEntry{path, stat->size, stat->modifiedTime});
        }
    }
    return result;
}

bool FileWatcher::start(const VirtualPath& root,
                        std::chrono::milliseconds debounce,
                        bool background) {
    stop();
    if (!root.valid() || root.scheme() != "asset" ||
        !FILE_SYSTEM.isDirectory(root)) {
        Log::error("FileWatcher", "Only an asset:// directory can be watched: %s",
                   root.string().c_str());
        return false;
    }
    root_ = root;
    debounce_ = std::clamp(debounce, std::chrono::milliseconds{100},
                           std::chrono::milliseconds{300});
    snapshot_ = makeSnapshot();
    {
        std::scoped_lock lock{mutex_};
        queued_.clear();
        pending_.clear();
        running_ = true;
    }
    if (background) {
        worker_ = std::jthread([this](std::stop_token stopToken) {
            while (!stopToken.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
                if (!stopToken.stop_requested()) scanNow();
            }
        });
    }
    return true;
}

void FileWatcher::stop() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    std::scoped_lock lock{mutex_};
    running_ = false;
    queued_.clear();
    pending_.clear();
    snapshot_.clear();
}

bool FileWatcher::running() const {
    std::scoped_lock lock{mutex_};
    return running_;
}

void FileWatcher::enqueue(FileChangeEvent event) {
    if (!event.path.valid() || event.path.scheme() != "asset" ||
        ignored(event.path)) {
        return;
    }
    std::scoped_lock lock{mutex_};
    queued_.push_back({std::move(event), std::chrono::steady_clock::now()});
}

void FileWatcher::scanNow() {
    {
        std::scoped_lock lock{mutex_};
        if (!running_) return;
    }
    auto current = makeSnapshot();
    std::vector<SnapshotEntry> removed;
    std::vector<SnapshotEntry> added;
    std::vector<FileChangeEvent> changes;
    {
        std::scoped_lock lock{mutex_};
        for (const auto& [key, oldEntry] : snapshot_) {
            const auto found = current.find(key);
            if (found == current.end()) {
                removed.push_back(oldEntry);
            } else if (found->second.size != oldEntry.size ||
                       found->second.modifiedTime != oldEntry.modifiedTime) {
                changes.push_back({FileChangeType::Modified,
                                   found->second.path, {}});
            }
        }
        for (const auto& [key, newEntry] : current) {
            if (!snapshot_.contains(key)) added.push_back(newEntry);
        }

        std::vector<bool> usedAdded(added.size());
        for (const SnapshotEntry& oldEntry : removed) {
            auto match = added.end();
            for (auto candidate = added.begin(); candidate != added.end(); ++candidate) {
                const std::size_t index =
                    static_cast<std::size_t>(candidate - added.begin());
                if (!usedAdded[index] && candidate->size == oldEntry.size &&
                    candidate->modifiedTime == oldEntry.modifiedTime) {
                    match = candidate;
                    usedAdded[index] = true;
                    break;
                }
            }
            if (match != added.end()) {
                changes.push_back({FileChangeType::Renamed, match->path,
                                   oldEntry.path});
            } else {
                changes.push_back({FileChangeType::Removed, oldEntry.path, {}});
            }
        }
        for (std::size_t index = 0; index < added.size(); ++index) {
            if (!usedAdded[index]) {
                changes.push_back({FileChangeType::Added, added[index].path, {}});
            }
        }
        snapshot_ = std::move(current);
    }
    for (FileChangeEvent& change : changes) enqueue(std::move(change));
}

std::vector<FileChangeEvent> FileWatcher::pollEvents() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<FileChangeEvent> result;
    std::scoped_lock lock{mutex_};
    for (TimedEvent& timed : queued_) {
        const std::string key = timed.event.path.string();
        auto found = pending_.find(key);
        if (found == pending_.end()) {
            pending_.emplace(key, std::move(timed));
            continue;
        }
        FileChangeEvent& previous = found->second.event;
        if (previous.type == FileChangeType::Added &&
            timed.event.type == FileChangeType::Removed) {
            pending_.erase(found);
            continue;
        }
        if (previous.type == FileChangeType::Added &&
            timed.event.type == FileChangeType::Modified) {
            found->second.time = timed.time;
            continue;
        }
        found->second = std::move(timed);
    }
    queued_.clear();
    for (auto entry = pending_.begin(); entry != pending_.end();) {
        if (now - entry->second.time >= debounce_) {
            result.push_back(std::move(entry->second.event));
            entry = pending_.erase(entry);
        } else {
            ++entry;
        }
    }
    return result;
}

} // namespace engine
