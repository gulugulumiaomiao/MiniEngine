#pragma once

// 内存 zip 读写封装（vendored miniz 3.0.2）。pimpl 隔离 miniz.h：引擎头文件
// 不暴露第三方 API。写侧在堆上累积条目（deflate），finalize 一次性产出归档
// 字节；读侧持有归档的自有拷贝，可重复提取任意条目。

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class ZipWriter final {
public:
    ZipWriter();
    ~ZipWriter();
    ZipWriter(ZipWriter&&) noexcept;
    ZipWriter& operator=(ZipWriter&&) noexcept;

    // 添加一个条目（deflate 压缩）。name 使用正斜杠相对路径；同名条目重复添加
    // 由读取侧的 locate 行为决定（后写覆盖前读），调用方应保证唯一。
    [[nodiscard]] bool addEntry(std::string_view name, std::span<const std::byte> data);

    // 结束归档并把字节写进 output；此后 writer 回到未初始化状态。
    [[nodiscard]] bool finalize(std::vector<std::byte>& output);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ZipReader final {
public:
    ZipReader();
    ~ZipReader();
    ZipReader(ZipReader&&) noexcept;
    ZipReader& operator=(ZipReader&&) noexcept;

    // 解析并接管归档字节的自有拷贝；非 zip 数据返回 nullopt。
    [[nodiscard]] static std::optional<ZipReader> open(std::span<const std::byte> data);

    // 条目名（跳过目录条目），按归档内顺序返回。
    [[nodiscard]] std::vector<std::string> entryNames() const;
    [[nodiscard]] bool hasEntry(std::string_view name) const;
    [[nodiscard]] std::size_t entryCount() const;
    // 解压单个条目；不存在或 CRC 校验失败返回 nullopt。
    [[nodiscard]] std::optional<std::vector<std::byte>> extract(std::string_view name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
