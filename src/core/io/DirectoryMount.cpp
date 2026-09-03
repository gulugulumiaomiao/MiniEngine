#include "core/io/DirectoryMount.h"

#include <algorithm>
#include <atomic>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace engine {

DirectoryMount::DirectoryMount(std::filesystem::path root, bool readOnly)
    : readOnly_(readOnly) {
  std::error_code error;
  root_ = std::filesystem::absolute(std::move(root), error).lexically_normal();
  if (error) {
    root_.clear();
  }
}

std::optional<std::filesystem::path>
DirectoryMount::resolvePhysicalPath(std::string_view relativePath) const {
  if (!valid()) {
    return std::nullopt;
  }
  const std::filesystem::path relative =
      std::filesystem::path{relativePath}.lexically_normal();
  if (relative.is_absolute()) {
    return std::nullopt;
  }
  const std::filesystem::path resolved = (root_ / relative).lexically_normal();
  const std::filesystem::path fromRoot = resolved.lexically_relative(root_);
  if (fromRoot.empty() && resolved != root_) {
    return std::nullopt;
  }
  const auto first = fromRoot.begin();
  if (first != fromRoot.end() && *first == "..") {
    return std::nullopt;
  }
  return resolved;
}

bool DirectoryMount::exists(std::string_view relativePath) const {
  const auto path = resolvePhysicalPath(relativePath);
  std::error_code error;
  return path && std::filesystem::exists(*path, error) && !error;
}

bool DirectoryMount::isFile(std::string_view relativePath) const {
  const auto path = resolvePhysicalPath(relativePath);
  std::error_code error;
  return path && std::filesystem::is_regular_file(*path, error) && !error;
}

bool DirectoryMount::isDirectory(std::string_view relativePath) const {
  const auto path = resolvePhysicalPath(relativePath);
  std::error_code error;
  return path && std::filesystem::is_directory(*path, error) && !error;
}

std::optional<FileStat>
DirectoryMount::stat(std::string_view relativePath) const {
  const auto path = resolvePhysicalPath(relativePath);
  if (!path) {
    return std::nullopt;
  }
  std::error_code error;
  const std::filesystem::file_status status =
      std::filesystem::status(*path, error);
  if (error || !std::filesystem::exists(status)) {
    return std::nullopt;
  }
  FileStat result;
  result.isFile = std::filesystem::is_regular_file(status);
  result.isDirectory = std::filesystem::is_directory(status);
  if (result.isFile) {
    result.size = std::filesystem::file_size(*path, error);
    if (error) {
      return std::nullopt;
    }
  }
  result.modifiedTime = std::filesystem::last_write_time(*path, error);
  return error ? std::nullopt : std::optional<FileStat>{result};
}

std::optional<std::vector<std::byte>>
DirectoryMount::readBinary(std::string_view relativePath) const {
  const auto path = resolvePhysicalPath(relativePath);
  if (!path) {
    return std::nullopt;
  }
  std::ifstream stream(*path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return std::nullopt;
  }
  const std::streamsize size = stream.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<std::byte> result(static_cast<std::size_t>(size));
  if (size > 0 && !stream.read(reinterpret_cast<char *>(result.data()), size)) {
    return std::nullopt;
  }
  return result;
}

bool DirectoryMount::writeBinary(std::string_view relativePath,
                                 std::span<const std::byte> data) {
  if (readOnly_) {
    return false;
  }
  const auto path = resolvePhysicalPath(relativePath);
  if (!path) {
    return false;
  }
  std::error_code error;
  if (!path->parent_path().empty()) {
    std::filesystem::create_directories(path->parent_path(), error);
    if (error) {
      return false;
    }
  }
  std::ofstream stream(*path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  if (!data.empty()) {
    stream.write(reinterpret_cast<const char *>(data.data()),
                 static_cast<std::streamsize>(data.size()));
  }
  return stream.good();
}

bool DirectoryMount::writeBinaryAtomic(std::string_view relativePath,
                                       std::span<const std::byte> data) {
  if (readOnly_) {
    return false;
  }
  const auto destination = resolvePhysicalPath(relativePath);
  if (!destination) {
    return false;
  }
  std::error_code error;
  if (!destination->parent_path().empty()) {
    std::filesystem::create_directories(destination->parent_path(), error);
    if (error) {
      return false;
    }
  }

  static std::atomic_uint64_t sequence{};
  std::filesystem::path temporary = *destination;
  temporary += ".tmp-" + std::to_string(++sequence);
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      return false;
    }
    if (!data.empty()) {
      stream.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
    stream.flush();
    if (!stream.good()) {
      stream.close();
      std::filesystem::remove(temporary, error);
      return false;
    }
  }

#ifdef _WIN32
  const bool replaced =
      MoveFileExW(temporary.c_str(), destination->c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!replaced) {
    std::filesystem::remove(temporary, error);
  }
  return replaced;
#else
  std::filesystem::rename(temporary, *destination, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
#endif
}

bool DirectoryMount::createDirectories(std::string_view relativePath) {
  if (readOnly_) {
    return false;
  }
  const auto path = resolvePhysicalPath(relativePath);
  if (!path) {
    return false;
  }
  std::error_code error;
  if (std::filesystem::is_directory(*path, error) && !error) {
    return true;
  }
  error.clear();
  return std::filesystem::create_directories(*path, error) && !error;
}

bool DirectoryMount::removeFile(std::string_view relativePath) {
  if (readOnly_) {
    return false;
  }
  const auto path = resolvePhysicalPath(relativePath);
  if (!path) {
    return false;
  }
  std::error_code error;
  return std::filesystem::is_regular_file(*path, error) && !error &&
         std::filesystem::remove(*path, error) && !error;
}

bool DirectoryMount::move(std::string_view from, std::string_view to) {
  if (readOnly_) {
    return false;
  }
  const auto source = resolvePhysicalPath(from);
  const auto destination = resolvePhysicalPath(to);
  if (!source || !destination) {
    return false;
  }
  std::error_code error;
  if (!destination->parent_path().empty()) {
    std::filesystem::create_directories(destination->parent_path(), error);
    if (error) {
      return false;
    }
  }
  if (std::filesystem::exists(*destination, error) || error) {
    return false;
  }
  std::filesystem::rename(*source, *destination, error);
  return !error;
}

std::vector<std::string>
DirectoryMount::listFiles(std::string_view relativePath, bool recursive) const {
  std::vector<std::string> result;
  const auto directory = resolvePhysicalPath(relativePath);
  if (!directory || !isDirectory(relativePath)) {
    return result;
  }
  std::error_code error;
  if (recursive) {
    std::filesystem::recursive_directory_iterator iterator{
        *directory, std::filesystem::directory_options::skip_permission_denied,
        error};
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
      if (iterator->is_regular_file(error) && !error) {
        result.push_back(
            iterator->path().lexically_relative(root_).generic_string());
      }
      iterator.increment(error);
    }
  } else {
    std::filesystem::directory_iterator iterator{
        *directory, std::filesystem::directory_options::skip_permission_denied,
        error};
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
      if (iterator->is_regular_file(error) && !error) {
        result.push_back(
            iterator->path().lexically_relative(root_).generic_string());
      }
      iterator.increment(error);
    }
  }
  std::ranges::sort(result);
  return result;
}

} // namespace engine
