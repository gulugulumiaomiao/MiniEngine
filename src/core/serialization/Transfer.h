#pragma once

#include "core/filesystem/VirtualPath.h"
#include "core/math/Math.h"
#include "core/serialization/Transferable.h"

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace engine {

enum class TransferMode { Read, Write };

class Transfer {
public:
    virtual ~Transfer() = default;

    [[nodiscard]] virtual TransferMode mode() const = 0;
    [[nodiscard]] bool reading() const { return mode() == TransferMode::Read; }
    [[nodiscard]] bool writing() const { return mode() == TransferMode::Write; }
    [[nodiscard]] bool valid() const { return error_.empty(); }
    [[nodiscard]] const std::string& error() const { return error_; }

    virtual bool beginObject(std::string_view name) = 0;
    virtual bool endObject() = 0;
    virtual bool beginArray(std::string_view name, std::uint32_t& size) = 0;
    virtual bool beginArrayElement(std::uint32_t index) = 0;
    virtual bool endArrayElement() = 0;
    virtual bool endArray() = 0;

    bool transfer(std::string_view name, bool& value);
    bool transfer(std::string_view name, std::int8_t& value);
    bool transfer(std::string_view name, std::uint8_t& value);
    bool transfer(std::string_view name, std::int16_t& value);
    bool transfer(std::string_view name, std::uint16_t& value);
    bool transfer(std::string_view name, std::int32_t& value);
    bool transfer(std::string_view name, std::uint32_t& value);
    bool transfer(std::string_view name, std::int64_t& value);
    bool transfer(std::string_view name, std::uint64_t& value);
    bool transfer(std::string_view name, float& value);
    bool transfer(std::string_view name, double& value);
    bool transfer(std::string_view name, std::string& value);
    bool transfer(std::string_view name, std::vector<std::byte>& value);
    bool transfer(std::string_view name, VirtualPath& value);
    bool transfer(std::string_view name, math::Vec2& value);
    bool transfer(std::string_view name, math::Vec3& value);
    bool transfer(std::string_view name, math::Vec4& value);
    bool transfer(std::string_view name, math::Mat33& value);
    bool transfer(std::string_view name, math::Mat44& value);
    bool transfer(std::string_view name, math::Quat& value);

    template <typename T>
        requires(std::is_enum_v<T>)
    bool transfer(std::string_view name, T& value) {
        using Encoded = std::underlying_type_t<T>;
        Encoded encoded = static_cast<Encoded>(value);
        if (!transfer(name, encoded))
            return false;
        if (reading())
            value = static_cast<T>(encoded);
        return true;
    }

    // 只定位字段；对象的 beginObject/endObject 由 Transferable 实现负责。
    bool transfer(std::string_view name, Transferable& value) {
        if (!valid() || !beginValue(name))
            return false;
        const bool result = value.transfer(*this);
        const bool ended = endValue();
        return result && ended && valid();
    }

    template <typename T> bool transfer(std::string_view name, std::vector<T>& values) {
        std::uint32_t size = writing() ? static_cast<std::uint32_t>(values.size()) : 0;
        if (writing() && values.size() > maxCollectionSize()) {
            return fail("Collection is too large");
        }
        if (!beginArray(name, size) || size > maxCollectionSize())
            return false;
        if (reading())
            values.resize(size);
        for (std::uint32_t index = 0; index < size; ++index) {
            if (!beginArrayElement(index) || !transfer({}, values[index]) || !endArrayElement()) {
                return false;
            }
        }
        return endArray();
    }

    template <typename T> bool transfer(std::string_view name, std::optional<T>& value) {
        // When reading, check if the field exists first
        if (reading()) {
            // Try to begin the object - if it fails, the field is missing
            if (!beginObject(name)) {
                // Field is missing, leave optional as nullopt
                clearError();  // Clear the error from failed beginObject
                value.reset();
                return true;
            }
        } else {
            // Writing: always create the object
            if (!beginObject(name))
                return false;
        }
        
        bool present = writing() ? value.has_value() : true;
        if (!transfer("has_value", present))
            return false;
        if (reading()) {
            if (present)
                value.emplace();
            else
                value.reset();
        }
        if (present && !transfer("value", *value))
            return false;
        return endObject();
    }

    template <typename... Types>
    bool transfer(std::string_view name, std::variant<Types...>& value) {
        if (!beginObject(name))
            return false;
        std::uint32_t index = writing() ? static_cast<std::uint32_t>(value.index()) : 0;
        if (!transfer("type", index))
            return false;
        if (index >= sizeof...(Types))
            return fail("Variant type is invalid");
        if (reading() && !emplaceVariant<0>(value, index)) {
            return fail("Variant type is invalid");
        }
        const bool result =
            std::visit([this](auto& item) { return this->transfer("value", item); }, value);
        return result && endObject();
    }

    [[nodiscard]] virtual std::uint32_t maxCollectionSize() const { return 1U << 20U; }

    // Resets a pending parse error so an optional field failure can be treated as
    // "field absent" instead of aborting the whole document.
    void clearError() { error_.clear(); }

protected:
    bool fail(std::string message);

private:
    template <std::size_t Index, typename... Types>
    bool emplaceVariant(std::variant<Types...>& value, std::uint32_t index) {
        if constexpr (Index == sizeof...(Types)) {
            return false;
        } else {
            if (index == Index) {
                value.template emplace<Index>();
                return true;
            }
            return emplaceVariant<Index + 1>(value, index);
        }
    }

    // 字段作用域不创建对象；结束时恢复进入前的位置，包括读取失败的情况。
    virtual bool beginValue(std::string_view name) = 0;
    virtual bool endValue() = 0;

    virtual bool transferBool(std::string_view name, bool& value) = 0;
    virtual bool transferInt8(std::string_view name, std::int8_t& value) = 0;
    virtual bool transferUInt8(std::string_view name, std::uint8_t& value) = 0;
    virtual bool transferInt16(std::string_view name, std::int16_t& value) = 0;
    virtual bool transferUInt16(std::string_view name, std::uint16_t& value) = 0;
    virtual bool transferInt32(std::string_view name, std::int32_t& value) = 0;
    virtual bool transferUInt32(std::string_view name, std::uint32_t& value) = 0;
    virtual bool transferInt64(std::string_view name, std::int64_t& value) = 0;
    virtual bool transferUInt64(std::string_view name, std::uint64_t& value) = 0;
    virtual bool transferFloat(std::string_view name, float& value) = 0;
    virtual bool transferDouble(std::string_view name, double& value) = 0;
    virtual bool transferString(std::string_view name, std::string& value) = 0;
    virtual bool transferBytes(std::string_view name, std::vector<std::byte>& value) = 0;

    std::string error_;
};

class Reader : public Transfer {
public:
    [[nodiscard]] TransferMode mode() const final { return TransferMode::Read; }
};

class Writer : public Transfer {
public:
    [[nodiscard]] TransferMode mode() const final { return TransferMode::Write; }
};

} // namespace engine
