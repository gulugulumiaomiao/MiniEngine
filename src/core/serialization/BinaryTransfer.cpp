#include "core/serialization/BinaryTransfer.h"

#include <bit>
#include <limits>
#include <type_traits>

namespace engine {

template <typename T>
bool BinaryWriter::writeInteger(T value) {
    using Unsigned = std::make_unsigned_t<T>;
    const Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes_.push_back(static_cast<std::byte>(
            (bits >> (index * 8U)) & static_cast<Unsigned>(0xffU)));
    }
    return true;
}

bool BinaryWriter::beginObject(std::string_view) { return valid(); }
bool BinaryWriter::endObject() { return valid(); }
bool BinaryWriter::beginArray(std::string_view, std::uint32_t& size) {
    return transfer({}, size);
}
bool BinaryWriter::beginArrayElement(std::uint32_t) { return valid(); }
bool BinaryWriter::endArrayElement() { return valid(); }
bool BinaryWriter::endArray() { return valid(); }

bool BinaryWriter::transferBool(std::string_view, bool& value) {
    return writeInteger<std::uint8_t>(value ? 1U : 0U);
}
#define MINI_BINARY_WRITE(Type, Name)                                          \
    bool BinaryWriter::Name(std::string_view, Type& value) {                   \
        return writeInteger(value);                                            \
    }
MINI_BINARY_WRITE(std::int8_t, transferInt8)
MINI_BINARY_WRITE(std::uint8_t, transferUInt8)
MINI_BINARY_WRITE(std::int16_t, transferInt16)
MINI_BINARY_WRITE(std::uint16_t, transferUInt16)
MINI_BINARY_WRITE(std::int32_t, transferInt32)
MINI_BINARY_WRITE(std::uint32_t, transferUInt32)
MINI_BINARY_WRITE(std::int64_t, transferInt64)
MINI_BINARY_WRITE(std::uint64_t, transferUInt64)
#undef MINI_BINARY_WRITE

bool BinaryWriter::transferFloat(std::string_view, float& value) {
    return writeInteger(std::bit_cast<std::uint32_t>(value));
}
bool BinaryWriter::transferDouble(std::string_view, double& value) {
    return writeInteger(std::bit_cast<std::uint64_t>(value));
}
bool BinaryWriter::transferString(std::string_view, std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return fail("String is too large");
    }
    writeInteger(static_cast<std::uint32_t>(value.size()));
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    return true;
}
bool BinaryWriter::transferBytes(std::string_view,
                                 std::vector<std::byte>& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        return fail("Byte block is too large");
    }
    writeInteger(static_cast<std::uint32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
    return true;
}

template <typename T>
bool BinaryReader::readInteger(T& value) {
    using Unsigned = std::make_unsigned_t<T>;
    if (remaining() < sizeof(T)) return fail("Unexpected end of binary data");
    Unsigned bits{};
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bits |= static_cast<Unsigned>(
                    std::to_integer<unsigned int>(bytes_[offset_ + index]))
                << (index * 8U);
    }
    offset_ += sizeof(T);
    value = static_cast<T>(bits);
    return true;
}

bool BinaryReader::beginObject(std::string_view) { return valid(); }
bool BinaryReader::endObject() { return valid(); }
bool BinaryReader::beginArray(std::string_view, std::uint32_t& size) {
    if (!transfer({}, size)) return false;
    return size <= maxCollectionSize() || fail("Collection is too large");
}
bool BinaryReader::beginArrayElement(std::uint32_t) { return valid(); }
bool BinaryReader::endArrayElement() { return valid(); }
bool BinaryReader::endArray() { return valid(); }

bool BinaryReader::transferBool(std::string_view, bool& value) {
    std::uint8_t encoded{};
    if (!readInteger(encoded) || encoded > 1U) return fail("Bool is invalid");
    value = encoded != 0;
    return true;
}
#define MINI_BINARY_READ(Type, Name)                                           \
    bool BinaryReader::Name(std::string_view, Type& value) {                   \
        return readInteger(value);                                             \
    }
MINI_BINARY_READ(std::int8_t, transferInt8)
MINI_BINARY_READ(std::uint8_t, transferUInt8)
MINI_BINARY_READ(std::int16_t, transferInt16)
MINI_BINARY_READ(std::uint16_t, transferUInt16)
MINI_BINARY_READ(std::int32_t, transferInt32)
MINI_BINARY_READ(std::uint32_t, transferUInt32)
MINI_BINARY_READ(std::int64_t, transferInt64)
MINI_BINARY_READ(std::uint64_t, transferUInt64)
#undef MINI_BINARY_READ

bool BinaryReader::transferFloat(std::string_view, float& value) {
    std::uint32_t bits{};
    if (!readInteger(bits)) return false;
    value = std::bit_cast<float>(bits);
    return true;
}
bool BinaryReader::transferDouble(std::string_view, double& value) {
    std::uint64_t bits{};
    if (!readInteger(bits)) return false;
    value = std::bit_cast<double>(bits);
    return true;
}
bool BinaryReader::transferString(std::string_view, std::string& value) {
    std::uint32_t size{};
    if (!readInteger(size) || size > 16U * 1024U * 1024U ||
        remaining() < size) return fail("String length is invalid");
    const char* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
    value.assign(begin, begin + size);
    offset_ += size;
    return true;
}
bool BinaryReader::transferBytes(std::string_view,
                                 std::vector<std::byte>& value) {
    std::uint32_t size{};
    if (!readInteger(size) || size > 1U * 1024U * 1024U * 1024U ||
        remaining() < size) return fail("Byte block length is invalid");
    value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                 bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
    offset_ += size;
    return true;
}

} // namespace engine
