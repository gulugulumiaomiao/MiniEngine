#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

struct FileStat {
  std::uintmax_t size{};
  std::filesystem::file_time_type modifiedTime{};
  bool isFile{};
  bool isDirectory{};
};

class IFileMount {
public:
  virtual ~IFileMount() = default;

  [[nodiscard]] virtual bool exists(std::string_view relativePath) const = 0;
  [[nodiscard]] virtual bool isFile(std::string_view relativePath) const = 0;
  [[nodiscard]] virtual bool
  isDirectory(std::string_view relativePath) const = 0;
  [[nodiscard]] virtual std::optional<FileStat>
  stat(std::string_view relativePath) const = 0;
  [[nodiscard]] virtual std::optional<std::vector<std::byte>>
  readBinary(std::string_view relativePath) const = 0;
  [[nodiscard]] virtual bool writeBinary(std::string_view relativePath,
                                         std::span<const std::byte> data) = 0;
  [[nodiscard]] virtual bool
  writeBinaryAtomic(std::string_view relativePath,
                    std::span<const std::byte> data) = 0;
  [[nodiscard]] virtual bool
  createDirectories(std::string_view relativePath) = 0;
  [[nodiscard]] virtual bool removeFile(std::string_view relativePath) = 0;
  [[nodiscard]] virtual bool move(std::string_view from,
                                  std::string_view to) = 0;
  [[nodiscard]] virtual std::vector<std::string>
  listFiles(std::string_view relativePath, bool recursive) const = 0;
  [[nodiscard]] virtual std::optional<std::filesystem::path>
  resolvePhysicalPath(std::string_view relativePath) const = 0;
  [[nodiscard]] virtual bool readOnly() const = 0;
};

} // namespace engine
