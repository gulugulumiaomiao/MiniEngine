#include "core/io/FileSystem.h"
#include "core/io/DirectoryMount.h"
#include "core/io/VirtualPath.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <string>

int main() {
  using namespace engine;

  const VirtualPath normalized{"Asset://folder/./nested/../file.txt"};
  const VirtualPath escaped{"asset://../outside.txt"};
  if (!normalized.valid() || normalized.scheme() != "asset" ||
      normalized.string() != "asset://folder/file.txt" || escaped.valid()) {
    return 1;
  }

  const auto unique =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("mini-vulkan-filesystem-test-" + std::to_string(unique));
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    return 2;
  }

  FileSystem &fileSystem = FILE_SYSTEM;
  if (!fileSystem.mountDirectory("test", root)) {
    return 3;
  }
  const VirtualPath textPath{"test://folder/message.txt"};
  const VirtualPath binaryPath{"test://data.bin"};
  const std::array<std::byte, 4> binary{std::byte{0x01}, std::byte{0x02},
                                        std::byte{0x03}, std::byte{0x04}};
  if (!fileSystem.writeText(textPath, "hello filesystem") ||
      !fileSystem.writeBinary(binaryPath, binary)) {
    return 4;
  }

  const auto text = fileSystem.readText(textPath);
  const auto bytes = fileSystem.readBinary(binaryPath);
  if (!text || *text != "hello filesystem" || !bytes ||
      bytes->size() != binary.size() || !std::ranges::equal(*bytes, binary)) {
    return 5;
  }
  if (!fileSystem.exists(textPath) || !fileSystem.isFile(textPath) ||
      !fileSystem.isDirectory(VirtualPath{"test://folder"})) {
    return 6;
  }

  const std::vector<VirtualPath> files =
      fileSystem.listFiles(VirtualPath{"test://"}, true);
  if (files.size() != 2 ||
      std::ranges::find_if(files, [&textPath](const VirtualPath &path) {
        return path.string() == textPath.string();
      }) == files.end()) {
    return 7;
  }

  const auto physical = fileSystem.resolvePhysicalPath(textPath);
  if (!physical || physical->lexically_normal() !=
                       (root / "folder/message.txt").lexically_normal()) {
    return 8;
  }
  const VirtualPath native = VirtualPath::fromNative(*physical);
  const auto nativeText = fileSystem.readText(native);
  if (!native.valid() || !native.isNative() || !nativeText ||
      *nativeText != "hello filesystem") {
    return 9;
  }

  DirectoryMount directoryMount{root};
  if (directoryMount.resolvePhysicalPath("../outside.txt")) {
    return 10;
  }
  if (!fileSystem.mountDirectory("readonly", root, true) ||
      fileSystem.writeText(VirtualPath{"readonly://blocked.txt"}, "blocked")) {
    return 11;
  }
  (void)fileSystem.unmount("readonly");

  const VirtualPath atomicPath{"test://atomic/state.json"};
  const VirtualPath movedPath{"test://moved/state.json"};
  if (!fileSystem.writeTextAtomic(atomicPath, "first") ||
      !fileSystem.writeTextAtomic(atomicPath, "second")) {
    return 12;
  }
  const auto atomicStat = fileSystem.stat(atomicPath);
  const auto virtualPath = fileSystem.toVirtualPath(root / "atomic/state.json");
  if (!atomicStat || !atomicStat->isFile || atomicStat->size != 6 ||
      !virtualPath || virtualPath->string() != atomicPath.string()) {
    return 13;
  }
  if (!fileSystem.createDirectories(VirtualPath{"test://moved"}) ||
      !fileSystem.move(atomicPath, movedPath) ||
      fileSystem.exists(atomicPath) || !fileSystem.exists(movedPath) ||
      !fileSystem.removeFile(movedPath) || fileSystem.exists(movedPath)) {
    return 14;
  }

  (void)fileSystem.unmount("test");
  std::filesystem::remove_all(root, error);
  return error ? 15 : 0;
}
