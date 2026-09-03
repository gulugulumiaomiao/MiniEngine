#pragma once

#include "core/io/IFileMount.h"

namespace engine {

class DirectoryMount final : public IFileMount {
public:
  explicit DirectoryMount(std::filesystem::path root, bool readOnly = false);

  [[nodiscard]] bool valid() const { return !root_.empty(); }
  [[nodiscard]] const std::filesystem::path &root() const { return root_; }
  [[nodiscard]] bool exists(std::string_view relativePath) const override;
  [[nodiscard]] bool isFile(std::string_view relativePath) const override;
  [[nodiscard]] bool isDirectory(std::string_view relativePath) const override;
  [[nodiscard]] std::optional<FileStat>
  stat(std::string_view relativePath) const override;
  [[nodiscard]] std::optional<std::vector<std::byte>>
  readBinary(std::string_view relativePath) const override;
  [[nodiscard]] bool writeBinary(std::string_view relativePath,
                                 std::span<const std::byte> data) override;
  [[nodiscard]] bool writeBinaryAtomic(
      std::string_view relativePath,
      std::span<const std::byte> data) override;
  [[nodiscard]] bool
  createDirectories(std::string_view relativePath) override;
  [[nodiscard]] bool removeFile(std::string_view relativePath) override;
  [[nodiscard]] bool move(std::string_view from,
                          std::string_view to) override;
  [[nodiscard]] std::vector<std::string>
  listFiles(std::string_view relativePath, bool recursive) const override;
  [[nodiscard]] std::optional<std::filesystem::path>
  resolvePhysicalPath(std::string_view relativePath) const override;
  [[nodiscard]] bool readOnly() const override { return readOnly_; }

private:
  std::filesystem::path root_;
  bool readOnly_{};
};

} // namespace engine
