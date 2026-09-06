#pragma once

#include "core/serialization/Transfer.h"

#include <span>

namespace engine {

class BinaryWriter final : public Writer {
public:
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }
    [[nodiscard]] std::vector<std::byte> takeBytes() { return std::move(bytes_); }

    bool beginObject(std::string_view name) override;
    bool endObject() override;
    bool beginArray(std::string_view name, std::uint32_t& size) override;
    bool beginArrayElement(std::uint32_t index) override;
    bool endArrayElement() override;
    bool endArray() override;

private:
    template <typename T>
    bool writeInteger(T value);

    bool transferBool(std::string_view, bool& value) override;
    bool transferInt8(std::string_view, std::int8_t& value) override;
    bool transferUInt8(std::string_view, std::uint8_t& value) override;
    bool transferInt16(std::string_view, std::int16_t& value) override;
    bool transferUInt16(std::string_view, std::uint16_t& value) override;
    bool transferInt32(std::string_view, std::int32_t& value) override;
    bool transferUInt32(std::string_view, std::uint32_t& value) override;
    bool transferInt64(std::string_view, std::int64_t& value) override;
    bool transferUInt64(std::string_view, std::uint64_t& value) override;
    bool transferFloat(std::string_view, float& value) override;
    bool transferDouble(std::string_view, double& value) override;
    bool transferString(std::string_view, std::string& value) override;
    bool transferBytes(std::string_view,
                       std::vector<std::byte>& value) override;

    std::vector<std::byte> bytes_;
};

class BinaryReader final : public Reader {
public:
    explicit BinaryReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] bool finished() const {
        return valid() && offset_ == bytes_.size();
    }

    bool beginObject(std::string_view name) override;
    bool endObject() override;
    bool beginArray(std::string_view name, std::uint32_t& size) override;
    bool beginArrayElement(std::uint32_t index) override;
    bool endArrayElement() override;
    bool endArray() override;

private:
    template <typename T>
    bool readInteger(T& value);

    bool transferBool(std::string_view, bool& value) override;
    bool transferInt8(std::string_view, std::int8_t& value) override;
    bool transferUInt8(std::string_view, std::uint8_t& value) override;
    bool transferInt16(std::string_view, std::int16_t& value) override;
    bool transferUInt16(std::string_view, std::uint16_t& value) override;
    bool transferInt32(std::string_view, std::int32_t& value) override;
    bool transferUInt32(std::string_view, std::uint32_t& value) override;
    bool transferInt64(std::string_view, std::int64_t& value) override;
    bool transferUInt64(std::string_view, std::uint64_t& value) override;
    bool transferFloat(std::string_view, float& value) override;
    bool transferDouble(std::string_view, double& value) override;
    bool transferString(std::string_view, std::string& value) override;
    bool transferBytes(std::string_view,
                       std::vector<std::byte>& value) override;

    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

} // namespace engine
