#include <gtest/gtest.h>

#include "core/archive/ZipArchive.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace engine;

std::vector<std::byte> toBytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data()) + text.size()};
}

std::string toString(std::span<const std::byte> data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

// 多条目（文本 + 二进制）roundtrip：字节逐位一致，条目可枚举可定位。
TEST(ZipArchiveTest, RoundTripsMultipleEntries) {
    const std::string text = "manifest payload \xE2\x9C\x93";
    std::vector<std::byte> binary;
    binary.reserve(256);
    for (std::size_t i = 0; i < 256; ++i)
        binary.push_back(static_cast<std::byte>(i));

    ZipWriter writer;
    ASSERT_TRUE(writer.addEntry("manifest.json", toBytes(text)));
    ASSERT_TRUE(writer.addEntry("assets/binary.bin", binary));

    std::vector<std::byte> archive;
    ASSERT_TRUE(writer.finalize(archive));
    ASSERT_FALSE(archive.empty());

    const auto reader = ZipReader::open(archive);
    ASSERT_TRUE(reader);
    EXPECT_EQ(reader->entryCount(), 2U);
    EXPECT_TRUE(reader->hasEntry("manifest.json"));
    EXPECT_TRUE(reader->hasEntry("assets/binary.bin"));
    EXPECT_FALSE(reader->hasEntry("missing.txt"));

    const auto extractedText = reader->extract("manifest.json");
    ASSERT_TRUE(extractedText);
    EXPECT_EQ(toString(*extractedText), text);
    const auto extractedBinary = reader->extract("assets/binary.bin");
    ASSERT_TRUE(extractedBinary);
    EXPECT_EQ(*extractedBinary, binary);
}

// 零长度条目是合法归档内容（空纹理/空 JSON 文件的镜像）。
TEST(ZipArchiveTest, RoundTripsEmptyEntry) {
    ZipWriter writer;
    ASSERT_TRUE(writer.addEntry("empty.txt", {}));
    std::vector<std::byte> archive;
    ASSERT_TRUE(writer.finalize(archive));

    const auto reader = ZipReader::open(archive);
    ASSERT_TRUE(reader);
    EXPECT_EQ(reader->entryCount(), 1U);
    const auto extracted = reader->extract("empty.txt");
    ASSERT_TRUE(extracted);
    EXPECT_TRUE(extracted->empty());
}

// 大段重复文本压缩后应显著小于原文（deflate 生效的冒烟检查）。
TEST(ZipArchiveTest, CompressesRepetitiveText) {
    const std::string repetitive(64 * 1024, 'x');
    ZipWriter writer;
    ASSERT_TRUE(writer.addEntry("big.txt", toBytes(repetitive)));
    std::vector<std::byte> archive;
    ASSERT_TRUE(writer.finalize(archive));
    EXPECT_LT(archive.size(), repetitive.size() / 4U);

    const auto reader = ZipReader::open(archive);
    ASSERT_TRUE(reader);
    const auto extracted = reader->extract("big.txt");
    ASSERT_TRUE(extracted);
    EXPECT_EQ(toString(*extracted), repetitive);
}

// UTF-8 文件名按字节原样存取：miniz 写读闭环自洽（跨解压工具的编码兼容性
// 不在本封装的保证范围内）。
TEST(ZipArchiveTest, RoundTripsUtf8EntryNames) {
    const std::string name = "assets/\xE8\xB5\x84\xE4\xBA\xA7/\xE6\x9D\x90\xE8\xB4\xA8.bin";
    const std::string payload = "utf-8 named entry";

    ZipWriter writer;
    ASSERT_TRUE(writer.addEntry(name, toBytes(payload)));
    std::vector<std::byte> archive;
    ASSERT_TRUE(writer.finalize(archive));

    const auto reader = ZipReader::open(archive);
    ASSERT_TRUE(reader);
    EXPECT_TRUE(reader->hasEntry(name));
    const auto names = reader->entryNames();
    ASSERT_EQ(names.size(), 1U);
    EXPECT_EQ(names.front(), name);
    const auto extracted = reader->extract(name);
    ASSERT_TRUE(extracted);
    EXPECT_EQ(toString(*extracted), payload);
}

// 非 zip 字节（空 buffer / 任意文本）必须被 open 拒绝。
TEST(ZipArchiveTest, RejectsInvalidArchiveData) {
    EXPECT_FALSE(ZipReader::open({}));
    EXPECT_FALSE(ZipReader::open(toBytes("this is definitely not a zip archive")));
}

// 条目名枚举按写入顺序返回，路径分隔符与嵌套目录不被改写。
TEST(ZipArchiveTest, EntryNamesPreserveOrderAndSlashes) {
    ZipWriter writer;
    ASSERT_TRUE(writer.addEntry("manifest.json", toBytes("{}")));
    ASSERT_TRUE(writer.addEntry("assets/a/b/c.dat", toBytes("c")));
    ASSERT_TRUE(writer.addEntry("assets/a/d.dat", toBytes("d")));
    std::vector<std::byte> archive;
    ASSERT_TRUE(writer.finalize(archive));

    const auto reader = ZipReader::open(archive);
    ASSERT_TRUE(reader);
    const auto names = reader->entryNames();
    ASSERT_EQ(names.size(), 3U);
    EXPECT_EQ(names[0], "manifest.json");
    EXPECT_EQ(names[1], "assets/a/b/c.dat");
    EXPECT_EQ(names[2], "assets/a/d.dat");
}

} // namespace
