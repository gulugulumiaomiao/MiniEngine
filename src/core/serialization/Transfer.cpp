#include "core/serialization/Transfer.h"

#include <utility>

namespace engine {

bool Transfer::fail(std::string message) {
    if (error_.empty()) error_ = std::move(message);
    return false;
}

#define MINI_TRANSFER_VALUE(Type, Method)                                      \
    bool Transfer::transfer(std::string_view name, Type& value) {              \
        return valid() && Method(name, value);                                 \
    }

MINI_TRANSFER_VALUE(bool, transferBool)
MINI_TRANSFER_VALUE(std::int8_t, transferInt8)
MINI_TRANSFER_VALUE(std::uint8_t, transferUInt8)
MINI_TRANSFER_VALUE(std::int16_t, transferInt16)
MINI_TRANSFER_VALUE(std::uint16_t, transferUInt16)
MINI_TRANSFER_VALUE(std::int32_t, transferInt32)
MINI_TRANSFER_VALUE(std::uint32_t, transferUInt32)
MINI_TRANSFER_VALUE(std::int64_t, transferInt64)
MINI_TRANSFER_VALUE(std::uint64_t, transferUInt64)
MINI_TRANSFER_VALUE(float, transferFloat)
MINI_TRANSFER_VALUE(double, transferDouble)
MINI_TRANSFER_VALUE(std::string, transferString)
MINI_TRANSFER_VALUE(std::vector<std::byte>, transferBytes)

#undef MINI_TRANSFER_VALUE

bool Transfer::transfer(std::string_view name, VirtualPath& value) {
    std::string path = writing() ? value.string() : std::string{};
    if (!transfer(name, path)) return false;
    if (reading()) {
        value = VirtualPath{path};
        if (!value.valid()) return fail("Virtual path is invalid");
    }
    return true;
}

bool Transfer::transfer(std::string_view name, math::Vec2& value) {
    std::uint32_t size = 2;
    if (!beginArray(name, size) || size != 2) return fail("Vec2 size is invalid");
    for (std::uint32_t index = 0; index < size; ++index) {
        if (!beginArrayElement(index) || !transfer({}, value[index]) ||
            !endArrayElement()) return false;
    }
    return endArray();
}

bool Transfer::transfer(std::string_view name, math::Vec3& value) {
    std::uint32_t size = 3;
    if (!beginArray(name, size) || size != 3) return fail("Vec3 size is invalid");
    for (std::uint32_t index = 0; index < size; ++index) {
        if (!beginArrayElement(index) || !transfer({}, value[index]) ||
            !endArrayElement()) return false;
    }
    return endArray();
}

bool Transfer::transfer(std::string_view name, math::Vec4& value) {
    std::uint32_t size = 4;
    if (!beginArray(name, size) || size != 4) return fail("Vec4 size is invalid");
    for (std::uint32_t index = 0; index < size; ++index) {
        if (!beginArrayElement(index) || !transfer({}, value[index]) ||
            !endArrayElement()) return false;
    }
    return endArray();
}

bool Transfer::transfer(std::string_view name, math::Mat33& value) {
    std::uint32_t size = 9;
    if (!beginArray(name, size) || size != 9) return fail("Mat33 size is invalid");
    for (std::uint32_t index = 0; index < size; ++index) {
        if (!beginArrayElement(index) ||
            !transfer({}, value[index / 3][index % 3]) ||
            !endArrayElement()) return false;
    }
    return endArray();
}

bool Transfer::transfer(std::string_view name, math::Mat44& value) {
    std::uint32_t size = 16;
    if (!beginArray(name, size) || size != 16) return fail("Mat44 size is invalid");
    for (std::uint32_t index = 0; index < size; ++index) {
        if (!beginArrayElement(index) ||
            !transfer({}, value[index / 4][index % 4]) ||
            !endArrayElement()) return false;
    }
    return endArray();
}

bool Transfer::transfer(std::string_view name, math::Quat& value) {
    math::Vec4 encoded{value.x, value.y, value.z, value.w};
    if (!transfer(name, encoded)) return false;
    if (reading()) value = math::Quat{encoded.w, encoded.x, encoded.y, encoded.z};
    return true;
}

} // namespace engine
