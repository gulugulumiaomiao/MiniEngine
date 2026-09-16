#include "core/archive/ZipArchive.h"

#include "miniz.h"

#include <cstring>
#include <utility>

namespace engine {

struct ZipWriter::Impl {
    mz_zip_archive archive{};
    bool valid{};
};

ZipWriter::ZipWriter() : impl_{std::make_unique<Impl>()} {
    impl_->valid = mz_zip_writer_init_heap(&impl_->archive, 0, 0) != 0;
}

ZipWriter::~ZipWriter() {
    if (impl_ && impl_->valid)
        mz_zip_writer_end(&impl_->archive);
}

ZipWriter::ZipWriter(ZipWriter&&) noexcept = default;
ZipWriter& ZipWriter::operator=(ZipWriter&&) noexcept = default;

bool ZipWriter::addEntry(std::string_view name, std::span<const std::byte> data) {
    if (!impl_ || !impl_->valid)
        return false;
    const std::string entryName{name};
    return mz_zip_writer_add_mem(&impl_->archive,
                                 entryName.c_str(),
                                 data.data(),
                                 data.size(),
                                 MZ_DEFAULT_COMPRESSION) != 0;
}

bool ZipWriter::finalize(std::vector<std::byte>& output) {
    if (!impl_ || !impl_->valid)
        return false;
    void* heap = nullptr;
    std::size_t size = 0;
    if (mz_zip_writer_finalize_heap_archive(&impl_->archive, &heap, &size) == 0)
        return false;
    // finalize_heap_archive 结束了 writer（相当于 writer_end），后续禁止再触
    // 碰 archive；置 invalid 让析构跳过重复 end。
    impl_->valid = false;
    output.assign(static_cast<const std::byte*>(heap), static_cast<const std::byte*>(heap) + size);
    mz_free(heap);
    return true;
}

struct ZipReader::Impl {
    mz_zip_archive archive{};
    std::vector<std::byte> owned;
    bool valid{};
};

ZipReader::ZipReader() = default;

ZipReader::~ZipReader() {
    if (impl_ && impl_->valid)
        mz_zip_reader_end(&impl_->archive);
}

ZipReader::ZipReader(ZipReader&&) noexcept = default;
ZipReader& ZipReader::operator=(ZipReader&&) noexcept = default;

std::optional<ZipReader> ZipReader::open(std::span<const std::byte> data) {
    ZipReader reader;
    reader.impl_ = std::make_unique<Impl>();
    reader.impl_->owned.assign(data.begin(), data.end());
    reader.impl_->valid =
        mz_zip_reader_init_mem(&reader.impl_->archive,
                               reader.impl_->owned.data(),
                               reader.impl_->owned.size(),
                               0) != 0;
    if (!reader.impl_->valid)
        return std::nullopt;
    return reader;
}

std::vector<std::string> ZipReader::entryNames() const {
    std::vector<std::string> names;
    if (!impl_ || !impl_->valid)
        return names;
    const mz_uint count = mz_zip_reader_get_num_files(&impl_->archive);
    names.reserve(count);
    char buffer[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
    for (mz_uint index = 0; index < count; ++index) {
        if (mz_zip_reader_is_file_a_directory(&impl_->archive, index))
            continue;
        const mz_uint length =
            mz_zip_reader_get_filename(&impl_->archive, index, buffer, sizeof(buffer));
        if (length == 0)
            continue;
        names.emplace_back(buffer, std::strlen(buffer));
    }
    return names;
}

bool ZipReader::hasEntry(std::string_view name) const {
    if (!impl_ || !impl_->valid)
        return false;
    const std::string entryName{name};
    return mz_zip_reader_locate_file(&impl_->archive, entryName.c_str(), nullptr, 0) >= 0;
}

std::size_t ZipReader::entryCount() const {
    if (!impl_ || !impl_->valid)
        return 0;
    const mz_uint count = mz_zip_reader_get_num_files(&impl_->archive);
    std::size_t files = 0;
    for (mz_uint index = 0; index < count; ++index) {
        if (!mz_zip_reader_is_file_a_directory(&impl_->archive, index))
            ++files;
    }
    return files;
}

std::optional<std::vector<std::byte>> ZipReader::extract(std::string_view name) const {
    if (!impl_ || !impl_->valid)
        return std::nullopt;
    const std::string entryName{name};
    std::size_t size = 0;
    void* heap = mz_zip_reader_extract_file_to_heap(&impl_->archive,
                                                    entryName.c_str(),
                                                    &size,
                                                    0);
    if (!heap)
        return std::nullopt;
    std::vector<std::byte> data{static_cast<const std::byte*>(heap),
                                static_cast<const std::byte*>(heap) + size};
    mz_free(heap);
    return data;
}

} // namespace engine
