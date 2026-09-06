#pragma once

#include "core/serialization/Transfer.h"

#include <memory>

namespace engine {

class JsonWriter final : public Writer {
public:
    JsonWriter();
    ~JsonWriter() override;
    JsonWriter(JsonWriter&&) noexcept;
    JsonWriter& operator=(JsonWriter&&) noexcept;

    [[nodiscard]] std::string toString(int indent = 2) const;

    bool beginObject(std::string_view name) override;
    bool endObject() override;
    bool beginArray(std::string_view name, std::uint32_t& size) override;
    bool beginArrayElement(std::uint32_t index) override;
    bool endArrayElement() override;
    bool endArray() override;

private:
    bool transferBool(std::string_view name, bool& value) override;
    bool transferInt8(std::string_view name, std::int8_t& value) override;
    bool transferUInt8(std::string_view name, std::uint8_t& value) override;
    bool transferInt16(std::string_view name, std::int16_t& value) override;
    bool transferUInt16(std::string_view name, std::uint16_t& value) override;
    bool transferInt32(std::string_view name, std::int32_t& value) override;
    bool transferUInt32(std::string_view name, std::uint32_t& value) override;
    bool transferInt64(std::string_view name, std::int64_t& value) override;
    bool transferUInt64(std::string_view name, std::uint64_t& value) override;
    bool transferFloat(std::string_view name, float& value) override;
    bool transferDouble(std::string_view name, double& value) override;
    bool transferString(std::string_view name, std::string& value) override;
    bool transferBytes(std::string_view name,
                       std::vector<std::byte>& value) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class JsonReader final : public Reader {
public:
    explicit JsonReader(std::string_view source);
    ~JsonReader() override;
    JsonReader(JsonReader&&) noexcept;
    JsonReader& operator=(JsonReader&&) noexcept;

    bool beginObject(std::string_view name) override;
    bool endObject() override;
    bool beginArray(std::string_view name, std::uint32_t& size) override;
    bool beginArrayElement(std::uint32_t index) override;
    bool endArrayElement() override;
    bool endArray() override;

private:
    bool transferBool(std::string_view name, bool& value) override;
    bool transferInt8(std::string_view name, std::int8_t& value) override;
    bool transferUInt8(std::string_view name, std::uint8_t& value) override;
    bool transferInt16(std::string_view name, std::int16_t& value) override;
    bool transferUInt16(std::string_view name, std::uint16_t& value) override;
    bool transferInt32(std::string_view name, std::int32_t& value) override;
    bool transferUInt32(std::string_view name, std::uint32_t& value) override;
    bool transferInt64(std::string_view name, std::int64_t& value) override;
    bool transferUInt64(std::string_view name, std::uint64_t& value) override;
    bool transferFloat(std::string_view name, float& value) override;
    bool transferDouble(std::string_view name, double& value) override;
    bool transferString(std::string_view name, std::string& value) override;
    bool transferBytes(std::string_view name,
                       std::vector<std::byte>& value) override;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine
