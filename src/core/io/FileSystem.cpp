#include "core/io/FileSystem.h"

#include "core/Log.h"
#include "core/io/DirectoryMount.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace engine {

FileSystem &FileSystem::instance() {
  static FileSystem fileSystem;
  return fileSystem;
}

std::string FileSystem::normalizeScheme(std::string_view scheme) {
  std::string result{scheme};
  std::ranges::transform(result, result.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  const VirtualPath probe{result + "://probe"};
  return probe.valid() && probe.scheme() != "file" ? result : std::string{};
}

bool FileSystem::mount(std::string_view scheme,
                       std::shared_ptr<IFileMount> fileMount) {
  const std::string normalized = normalizeScheme(scheme);
  if (normalized.empty() || !fileMount) {
    Log::error("FileSystem", "Cannot mount invalid scheme: %.*s",
               static_cast<int>(scheme.size()), scheme.data());
    return false;
  }
  std::scoped_lock lock{mutex_};
  mounts_[normalized] = std::move(fileMount);
  return true;
}

bool FileSystem::mountDirectory(std::string_view scheme,
                                const std::filesystem::path &directory,
                                bool readOnly) {
  auto directoryMount = std::make_shared<DirectoryMount>(directory, readOnly);
  if (!directoryMount->valid()) {
    Log::error("FileSystem", "Cannot mount directory: %s",
               directory.string().c_str());
    return false;
  }
  return mount(scheme, std::move(directoryMount));
}

bool FileSystem::unmount(std::string_view scheme) {
  const std::string normalized = normalizeScheme(scheme);
  if (normalized.empty()) {
    return false;
  }
  std::scoped_lock lock{mutex_};
  return mounts_.erase(normalized) != 0;
}

std::optional<FileSystem::MountLookup>
FileSystem::lookup(const VirtualPath &path, bool reportFailure) const {
  if (!path.valid()) {
    if (reportFailure) {
      Log::error("FileSystem", "Invalid virtual path: %s",
                 path.string().c_str());
    }
    return std::nullopt;
  }
  if (path.isNative()) {
    const std::filesystem::path native = path.nativePath();
    const std::filesystem::path root = native.root_path();
    const std::filesystem::path relative = native.lexically_relative(root);
    if (root.empty() || relative.empty()) {
      if (reportFailure) {
        Log::error("FileSystem", "Invalid native path: %s",
                   path.string().c_str());
      }
      return std::nullopt;
    }
    return MountLookup{std::make_shared<DirectoryMount>(root),
                       relative.generic_string()};
  }

  std::shared_ptr<IFileMount> fileMount;
  {
    std::scoped_lock lock{mutex_};
    const auto found = mounts_.find(path.scheme());
    if (found != mounts_.end()) {
      fileMount = found->second;
    }
  }
  if (!fileMount) {
    if (reportFailure) {
      Log::error("FileSystem", "Scheme is not mounted: %s",
                 path.scheme().c_str());
    }
    return std::nullopt;
  }
  return MountLookup{std::move(fileMount), path.relativePath()};
}

bool FileSystem::exists(const VirtualPath &path) const {
  const auto found = lookup(path, false);
  return found && found->mount->exists(found->relativePath);
}

bool FileSystem::isFile(const VirtualPath &path) const {
  const auto found = lookup(path, false);
  return found && found->mount->isFile(found->relativePath);
}

bool FileSystem::isDirectory(const VirtualPath &path) const {
  const auto found = lookup(path, false);
  return found && found->mount->isDirectory(found->relativePath);
}

std::optional<FileStat> FileSystem::stat(const VirtualPath &path) const {
  const auto found = lookup(path, false);
  return found ? found->mount->stat(found->relativePath) : std::nullopt;
}

std::optional<std::vector<std::byte>>
FileSystem::readBinary(const VirtualPath &path) const {
  const auto found = lookup(path);
  if (!found) {
    return std::nullopt;
  }
  auto content = found->mount->readBinary(found->relativePath);
  if (!content) {
    Log::warn("FileSystem", "Cannot read file: %s", path.string().c_str());
  }
  return content;
}

std::optional<std::string> FileSystem::readText(const VirtualPath &path) const {
  const auto bytes = readBinary(path);
  if (!bytes) {
    return std::nullopt;
  }
  std::string text(bytes->size(), '\0');
  if (!bytes->empty()) {
    std::memcpy(text.data(), bytes->data(), bytes->size());
  }
  return text;
}

bool FileSystem::writeBinary(const VirtualPath &path,
                             std::span<const std::byte> content) {
  const auto found = lookup(path);
  if (!found) {
    return false;
  }
  if (found->mount->readOnly()) {
    Log::error("FileSystem", "Cannot write to read-only mount: %s",
               path.string().c_str());
    return false;
  }
  if (!found->mount->writeBinary(found->relativePath, content)) {
    Log::error("FileSystem", "Cannot write file: %s", path.string().c_str());
    return false;
  }
  return true;
}

bool FileSystem::writeText(const VirtualPath &path, std::string_view content) {
  return writeBinary(path, {reinterpret_cast<const std::byte *>(content.data()),
                            content.size()});
}

bool FileSystem::writeBinaryAtomic(const VirtualPath &path,
                                   std::span<const std::byte> content) {
  const auto found = lookup(path);
  if (!found || found->mount->readOnly()) {
    Log::error("FileSystem", "Cannot atomically write file: %s",
               path.string().c_str());
    return false;
  }
  if (!found->mount->writeBinaryAtomic(found->relativePath, content)) {
    Log::error("FileSystem", "Cannot atomically write file: %s",
               path.string().c_str());
    return false;
  }
  return true;
}

bool FileSystem::writeTextAtomic(const VirtualPath &path,
                                 std::string_view content) {
  return writeBinaryAtomic(
      path, {reinterpret_cast<const std::byte*>(content.data()), content.size()});
}

bool FileSystem::createDirectories(const VirtualPath &path) {
  const auto found = lookup(path);
  if (!found || found->mount->readOnly() ||
      !found->mount->createDirectories(found->relativePath)) {
    Log::error("FileSystem", "Cannot create directory: %s",
               path.string().c_str());
    return false;
  }
  return true;
}

bool FileSystem::removeFile(const VirtualPath &path) {
  const auto found = lookup(path);
  if (!found || found->mount->readOnly() ||
      !found->mount->removeFile(found->relativePath)) {
    Log::error("FileSystem", "Cannot remove file: %s", path.string().c_str());
    return false;
  }
  return true;
}

bool FileSystem::move(const VirtualPath &from, const VirtualPath &to) {
  if (!from.valid() || !to.valid() || from.scheme() != to.scheme()) {
    Log::error("FileSystem", "Move requires paths in the same mount");
    return false;
  }
  const auto source = lookup(from);
  const auto destination = lookup(to);
  if (!source || !destination || source->mount != destination->mount ||
      source->mount->readOnly() ||
      !source->mount->move(source->relativePath, destination->relativePath)) {
    Log::error("FileSystem", "Cannot move file: %s -> %s",
               from.string().c_str(), to.string().c_str());
    return false;
  }
  return true;
}

std::vector<VirtualPath> FileSystem::listFiles(const VirtualPath &directory,
                                               bool recursive) const {
  std::vector<VirtualPath> result;
  const auto found = lookup(directory);
  if (!found) {
    return result;
  }
  const std::vector<std::string> files =
      found->mount->listFiles(found->relativePath, recursive);
  result.reserve(files.size());
  for (const std::string &file : files) {
    if (directory.isNative()) {
      const auto physical = found->mount->resolvePhysicalPath(file);
      if (physical) {
        result.push_back(VirtualPath::fromNative(*physical));
      }
    } else {
      result.emplace_back(directory.scheme() + "://" + file);
    }
  }
  return result;
}

std::optional<std::filesystem::path>
FileSystem::resolvePhysicalPath(const VirtualPath &path) const {
  const auto found = lookup(path);
  return found ? found->mount->resolvePhysicalPath(found->relativePath)
               : std::nullopt;
}

std::optional<VirtualPath>
FileSystem::toVirtualPath(const std::filesystem::path &physicalPath) const {
  std::error_code error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(physicalPath, error).lexically_normal();
  if (error) {
    return std::nullopt;
  }

  std::vector<std::pair<std::string, std::shared_ptr<IFileMount>>> mounts;
  {
    std::scoped_lock lock{mutex_};
    mounts.reserve(mounts_.size());
    for (const auto& entry : mounts_) {
      mounts.push_back(entry);
    }
  }

  std::optional<VirtualPath> best;
  std::size_t bestRootLength = 0;
  for (const auto& [scheme, mount] : mounts) {
    const auto root = mount->resolvePhysicalPath("");
    if (!root) {
      continue;
    }
    const std::filesystem::path relative = absolute.lexically_relative(*root);
    if (relative.empty() && absolute != root->lexically_normal()) {
      continue;
    }
    const auto first = relative.begin();
    if (first != relative.end() && *first == "..") {
      continue;
    }
    const std::size_t rootLength = root->generic_string().size();
    if (!best || rootLength > bestRootLength) {
      best = VirtualPath{scheme + "://" + relative.generic_string()};
      bestRootLength = rootLength;
    }
  }
  return best;
}

} // namespace engine
