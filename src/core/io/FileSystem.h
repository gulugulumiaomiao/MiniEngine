#pragma once

#include "core/io/IFileMount.h"
#include "core/io/VirtualPath.h"

#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class FileSystem final {
public:
  [[nodiscard]] static FileSystem &instance();

  FileSystem(const FileSystem &) = delete;
  FileSystem &operator=(const FileSystem &) = delete;

  [[nodiscard]] bool mount(std::string_view scheme,
                           std::shared_ptr<IFileMount> fileMount);
  [[nodiscard]] bool mountDirectory(std::string_view scheme,
                                    const std::filesystem::path &directory,
                                    bool readOnly = false);
  [[nodiscard]] bool unmount(std::string_view scheme);

  [[nodiscard]] bool exists(const VirtualPath &path) const;
  [[nodiscard]] bool isFile(const VirtualPath &path) const;
  [[nodiscard]] bool isDirectory(const VirtualPath &path) const;
  [[nodiscard]] std::optional<FileStat> stat(const VirtualPath &path) const;

  [[nodiscard]] std::optional<std::string>
  readText(const VirtualPath &path) const;
  [[nodiscard]] std::optional<std::vector<std::byte>>
  readBinary(const VirtualPath &path) const;
  [[nodiscard]] bool writeText(const VirtualPath &path,
                               std::string_view content);
  [[nodiscard]] bool writeBinary(const VirtualPath &path,
                                 std::span<const std::byte> content);
  [[nodiscard]] bool writeTextAtomic(const VirtualPath &path,
                                     std::string_view content);
  [[nodiscard]] bool writeBinaryAtomic(
      const VirtualPath &path, std::span<const std::byte> content);
  [[nodiscard]] bool createDirectories(const VirtualPath &path);
  [[nodiscard]] bool removeFile(const VirtualPath &path);
  [[nodiscard]] bool move(const VirtualPath &from, const VirtualPath &to);

  [[nodiscard]] std::vector<VirtualPath>
  listFiles(const VirtualPath &directory, bool recursive = false) const;
  [[nodiscard]] std::optional<std::filesystem::path>
  resolvePhysicalPath(const VirtualPath &path) const;
  [[nodiscard]] std::optional<VirtualPath>
  toVirtualPath(const std::filesystem::path &physicalPath) const;

private:
  FileSystem() = default;

  struct MountLookup {
    std::shared_ptr<IFileMount> mount;
    std::string relativePath;
  };

  [[nodiscard]] std::optional<MountLookup>
  lookup(const VirtualPath &path, bool reportFailure = true) const;
  [[nodiscard]] static std::string normalizeScheme(std::string_view scheme);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<IFileMount>> mounts_;
};

} // namespace engine

#define FILE_SYSTEM (::engine::FileSystem::instance())
