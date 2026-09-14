#include "core/hash.h"

#include "core/filesystem/FileSystem.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

enum class Example : std::uint16_t { Value = 0x1234 };

// "hello" as raw bytes; kept out of the assertion macros because a braced
// initializer list inside EXPECT_* would split on its commas at preprocessing.
constexpr std::array kFileBytes{
    std::byte{0x68}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}};

} // namespace

TEST(HashTest, FnvVectorsMatch) {
    EXPECT_EQ(engine::hashString(""), 0xcbf29ce484222325ULL);
    EXPECT_EQ(engine::hashString("a"), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(engine::hashString("hello"), 0xa430d84680aabd0bULL);
}

TEST(HashTest, IncrementalMatchesContiguous) {
    engine::Hash64 incremental = engine::hashString("hel");
    incremental = engine::hashString("lo", incremental);
    EXPECT_EQ(incremental, engine::hashString("hello"));
}

TEST(HashTest, TypedHashingEncodesLittleEndian) {
    engine::Hash64 typed = engine::kFnv1a64OffsetBasis;
    engine::hashAppend(typed, std::uint16_t{0x1234});
    engine::hashAppend(typed, Example::Value);
    engine::hashAppend(typed, true);
    const std::array expectedBytes{
        std::byte{0x34}, std::byte{0x12}, std::byte{0x34}, std::byte{0x12}, std::byte{0x01}};
    EXPECT_EQ(typed, engine::hashBytes(expectedBytes));
}

TEST(HashTest, HexFormattingIsZeroPadded) {
    EXPECT_EQ(engine::hashToHex(0x1234ULL), "0000000000001234");
}

TEST(HashTest, MixingChangesItsInput) {
    EXPECT_NE(engine::mixHash64(1), 1ULL);
    EXPECT_NE(engine::combineHash(10, 20), 10ULL);
}

class HashFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("mini-engine-hash-test-" + std::to_string(unique));
        std::error_code error;
        std::filesystem::create_directories(root, error);
        ASSERT_FALSE(error) << "Cannot create hash test directory";
        // FILE_SYSTEM is a macro that expands to a fully qualified call,
        // so it must not carry an extra engine:: prefix.
        ASSERT_TRUE(FILE_SYSTEM.mountDirectory("hash-test", root))
            << "Cannot mount hash test directory";
    }

    void TearDown() override {
        // Best-effort cleanup: TearDown also runs when SetUp bailed out before
        // the mount succeeded, so an unmount failure here must not fail the test.
        (void)FILE_SYSTEM.unmount("hash-test");
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
};

TEST_F(HashFileTest, FileHashMatchesMemoryHash) {
    const engine::VirtualPath file{"hash-test://content.bin"};
    ASSERT_TRUE(FILE_SYSTEM.writeBinaryAtomic(file, kFileBytes))
        << "Cannot write hash test file";
    const auto fileHash = engine::hashFile(file);
    ASSERT_TRUE(fileHash.has_value());
    EXPECT_EQ(*fileHash, engine::hashBytes(kFileBytes));
}
