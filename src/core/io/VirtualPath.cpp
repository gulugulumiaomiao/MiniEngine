#include "core/io/VirtualPath.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace engine {
namespace {

bool validScheme(std::string_view scheme) {
  if (scheme.empty() ||
      std::isalpha(static_cast<unsigned char>(scheme.front())) == 0) {
    return false;
  }
  return std::ranges::all_of(scheme, [](char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '+' || character == '-' ||
           character == '_';
  });
}

std::string normalizeRelativePath(std::string_view path, bool &valid) {
  std::string source{path};
  std::ranges::replace(source, '\\', '/');
  std::vector<std::string> segments;
  std::size_t begin = 0;
  while (begin <= source.size()) {
    const std::size_t end = source.find('/', begin);
    const std::string segment = source.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!segment.empty() && segment != ".") {
      if (segment == "..") {
        if (segments.empty()) {
          valid = false;
          return {};
        }
        segments.pop_back();
      } else {
        segments.push_back(segment);
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  std::string normalized;
  for (const std::string &segment : segments) {
    if (!normalized.empty()) {
      normalized.push_back('/');
    }
    normalized += segment;
  }
  return normalized;
}

} // namespace

VirtualPath::VirtualPath(std::string_view path) {
  const std::size_t separator = path.find("://");
  if (separator == std::string_view::npos) {
    return;
  }
  scheme_ = std::string{path.substr(0, separator)};
  std::ranges::transform(scheme_, scheme_.begin(), [](char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  if (!validScheme(scheme_)) {
    scheme_.clear();
    return;
  }

  const std::string_view payload = path.substr(separator + 3);
  if (scheme_ == "file") {
    std::string native{payload};
    std::ranges::replace(native, '/',
                         std::filesystem::path::preferred_separator);
    const std::filesystem::path normalized =
        std::filesystem::path{native}.lexically_normal();
    if (!normalized.is_absolute()) {
      scheme_.clear();
      return;
    }
    relativePath_ = normalized.generic_string();
  } else {
    bool normalizedSuccessfully = true;
    relativePath_ = normalizeRelativePath(payload, normalizedSuccessfully);
    if (!normalizedSuccessfully) {
      scheme_.clear();
      relativePath_.clear();
      return;
    }
  }
  string_ = scheme_ + "://" + relativePath_;
  valid_ = true;
}

VirtualPath VirtualPath::fromNative(const std::filesystem::path &path) {
  VirtualPath result;
  if (!path.is_absolute()) {
    return result;
  }
  result.scheme_ = "file";
  result.relativePath_ = path.lexically_normal().generic_string();
  result.string_ = result.scheme_ + "://" + result.relativePath_;
  result.valid_ = true;
  return result;
}

std::filesystem::path VirtualPath::nativePath() const {
  return isNative() && valid_ ? std::filesystem::path{relativePath_}
                              : std::filesystem::path{};
}

VirtualPath VirtualPath::parent() const {
  if (!valid_) return {};
  const std::filesystem::path path{relativePath_};
  if (isNative()) return fromNative(path.parent_path());
  return VirtualPath{scheme_ + "://" + path.parent_path().generic_string()};
}

VirtualPath VirtualPath::joined(std::string_view relativePath) const {
  if (!valid_) return {};
  if (VirtualPath absolute{relativePath}; absolute.valid()) return absolute;
  const std::filesystem::path joined =
      (std::filesystem::path{relativePath_} /
       std::filesystem::path{relativePath})
          .lexically_normal();
  if (isNative()) return fromNative(joined);
  return VirtualPath{scheme_ + "://" + joined.generic_string()};
}

std::string VirtualPath::filename() const {
  return valid_ ? std::filesystem::path{relativePath_}.filename().generic_string()
                : std::string{};
}

std::string VirtualPath::extension() const {
  return valid_ ? std::filesystem::path{relativePath_}.extension().string()
                : std::string{};
}

} // namespace engine
