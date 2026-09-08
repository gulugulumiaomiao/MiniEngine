#include "core/hash.h"

#include "core/filesystem/FileSystem.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

enum class Example : std::uint16_t { Value = 0x1234 };

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() {
    using namespace engine;

    if (hashString("") != 0xcbf29ce484222325ULL || hashString("a") != 0xaf63dc4c8601ec8cULL ||
        hashString("hello") != 0xa430d84680aabd0bULL) {
        return fail("FNV-1a test vectors do not match");
    }

    Hash64 incremental = hashString("hel");
    incremental = hashString("lo", incremental);
    if (incremental != hashString("hello"))
        return fail("Incremental string hashing does not match contiguous hashing");

    Hash64 typed = kFnv1a64OffsetBasis;
    hashAppend(typed, std::uint16_t{0x1234});
    hashAppend(typed, Example::Value);
    hashAppend(typed, true);
    const std::array expectedBytes{
        std::byte{0x34}, std::byte{0x12}, std::byte{0x34}, std::byte{0x12}, std::byte{0x01}};
    if (typed != hashBytes(expectedBytes))
        return fail("Typed hashing is not deterministic little-endian encoding");

    if (hashToHex(0x1234ULL) != "0000000000001234")
        return fail("Hash hex formatting is invalid");
    if (mixHash64(1) == 1 || combineHash(10, 20) == 10)
        return fail("Hash mixing did not change its input");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       ("mini-engine-hash-test-" + std::to_string(unique));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
        return fail("Cannot create hash test directory");
    if (!FILE_SYSTEM.mountDirectory("hash-test", root))
        return fail("Cannot mount hash test directory");
    const VirtualPath file{"hash-test://content.bin"};
    const std::array fileBytes{
        std::byte{0x68}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}};
    if (!FILE_SYSTEM.writeBinaryAtomic(file, fileBytes))
        return fail("Cannot write hash test file");
    const auto fileHash = hashFile(file);
    if (!fileHash || *fileHash != hashBytes(fileBytes))
        return fail("File and memory hashing do not match");
    std::filesystem::remove(root / "content.bin", error);
    std::filesystem::remove(root, error);
    return 0;
}
