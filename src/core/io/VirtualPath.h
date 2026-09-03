#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace engine {

class VirtualPath final {
public:
  VirtualPath() = default;
  explicit VirtualPath(std::string_view path);

  [[nodiscard]] static VirtualPath
  fromNative(const std::filesystem::path &path);

  [[nodiscard]] bool valid() const { return valid_; }
  [[nodiscard]] bool empty() const { return string_.empty(); }
  [[nodiscard]] bool isNative() const { return scheme_ == "file"; }
  [[nodiscard]] const std::string &scheme() const { return scheme_; }
  [[nodiscard]] const std::string &relativePath() const {
    return relativePath_;
  }
  [[nodiscard]] const std::string &string() const { return string_; }
  [[nodiscard]] std::filesystem::path nativePath() const;
  [[nodiscard]] VirtualPath parent() const;
  [[nodiscard]] VirtualPath joined(std::string_view relativePath) const;
  [[nodiscard]] std::string filename() const;
  [[nodiscard]] std::string extension() const;
  bool operator==(const VirtualPath &) const = default;

private:
  std::string scheme_;
  std::string relativePath_;
  std::string string_;
  bool valid_{};
};

} // namespace engine
