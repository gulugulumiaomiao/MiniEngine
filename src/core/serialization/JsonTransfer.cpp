#include "core/serialization/JsonTransfer.h"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <utility>

namespace engine {
namespace {

using Json = nlohmann::json;

Json* selectWrite(Json& root, std::vector<Json*>& stack,
                  std::string_view name) {
    if (stack.empty()) return &root;
    Json* current = stack.back();
    if (name.empty()) return current;
    if (!current->is_object()) *current = Json::object();
    return &(*current)[std::string{name}];
}

const Json* selectRead(const Json& root, const std::vector<const Json*>& stack,
                       std::string_view name) {
    if (stack.empty()) return &root;
    const Json* current = stack.back();
    if (name.empty()) return current;
    if (!current->is_object()) return nullptr;
    const auto found = current->find(name);
    return found == current->end() ? nullptr : &*found;
}

template <typename T>
bool readSigned(const Json* source, T& value) {
    if (!source || (!source->is_number_integer() &&
                    !source->is_number_unsigned())) return false;
    if (source->is_number_unsigned()) {
        const std::uint64_t encoded = source->get<std::uint64_t>();
        if (encoded > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
            return false;
        }
        value = static_cast<T>(encoded);
        return true;
    }
    const std::int64_t encoded = source->get<std::int64_t>();
    if (encoded < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
        encoded > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    value = static_cast<T>(encoded);
    return true;
}

template <typename T>
bool readUnsigned(const Json* source, T& value) {
    if (!source || !source->is_number_unsigned()) return false;
    const std::uint64_t encoded = source->get<std::uint64_t>();
    if (encoded > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        return false;
    }
    value = static_cast<T>(encoded);
    return true;
}

} // namespace

struct JsonWriter::Impl {
    Json root;
    std::vector<Json*> stack;
};

JsonWriter::JsonWriter() : impl_(std::make_unique<Impl>()) {}
JsonWriter::~JsonWriter() = default;
JsonWriter::JsonWriter(JsonWriter&&) noexcept = default;
JsonWriter& JsonWriter::operator=(JsonWriter&&) noexcept = default;

std::string JsonWriter::toString(int indent) const {
    return impl_->root.dump(indent);
}

bool JsonWriter::beginObject(std::string_view name) {
    Json* value = selectWrite(impl_->root, impl_->stack, name);
    *value = Json::object();
    impl_->stack.push_back(value);
    return true;
}
bool JsonWriter::endObject() {
    if (impl_->stack.empty()) return fail("JSON object stack is empty");
    impl_->stack.pop_back();
    return true;
}
bool JsonWriter::beginArray(std::string_view name, std::uint32_t& size) {
    Json* value = selectWrite(impl_->root, impl_->stack, name);
    *value = Json::array();
    value->get_ref<Json::array_t&>().resize(size);
    impl_->stack.push_back(value);
    return true;
}
bool JsonWriter::beginArrayElement(std::uint32_t index) {
    if (impl_->stack.empty() || !impl_->stack.back()->is_array() ||
        index >= impl_->stack.back()->size()) {
        return fail("JSON array index is invalid");
    }
    impl_->stack.push_back(&(*impl_->stack.back())[index]);
    return true;
}
bool JsonWriter::endArrayElement() { return endObject(); }
bool JsonWriter::endArray() { return endObject(); }

#define MINI_JSON_WRITE(Type, Name)                                            \
    bool JsonWriter::Name(std::string_view name, Type& value) {                \
        *selectWrite(impl_->root, impl_->stack, name) = value;                 \
        return true;                                                           \
    }
MINI_JSON_WRITE(bool, transferBool)
MINI_JSON_WRITE(std::int8_t, transferInt8)
MINI_JSON_WRITE(std::uint8_t, transferUInt8)
MINI_JSON_WRITE(std::int16_t, transferInt16)
MINI_JSON_WRITE(std::uint16_t, transferUInt16)
MINI_JSON_WRITE(std::int32_t, transferInt32)
MINI_JSON_WRITE(std::uint32_t, transferUInt32)
MINI_JSON_WRITE(std::int64_t, transferInt64)
MINI_JSON_WRITE(std::uint64_t, transferUInt64)
MINI_JSON_WRITE(std::string, transferString)
#undef MINI_JSON_WRITE

bool JsonWriter::transferFloat(std::string_view name, float& value) {
    if (!std::isfinite(value)) return fail("Cannot write a non-finite float");
    *selectWrite(impl_->root, impl_->stack, name) = value;
    return true;
}

bool JsonWriter::transferDouble(std::string_view name, double& value) {
    if (!std::isfinite(value)) return fail("Cannot write a non-finite double");
    *selectWrite(impl_->root, impl_->stack, name) = value;
    return true;
}

bool JsonWriter::transferBytes(std::string_view name,
                               std::vector<std::byte>& value) {
    Json* destination = selectWrite(impl_->root, impl_->stack, name);
    *destination = Json::array();
    auto& array = destination->get_ref<Json::array_t&>();
    array.reserve(value.size());
    for (std::byte byte : value) {
        array.push_back(std::to_integer<std::uint8_t>(byte));
    }
    return true;
}

struct JsonReader::Impl {
    Json root;
    std::vector<const Json*> stack;
};

JsonReader::JsonReader(std::string_view source) : impl_(std::make_unique<Impl>()) {
    impl_->root = Json::parse(source, nullptr, false);
    if (impl_->root.is_discarded()) fail("Invalid JSON document");
}
JsonReader::~JsonReader() = default;
JsonReader::JsonReader(JsonReader&&) noexcept = default;
JsonReader& JsonReader::operator=(JsonReader&&) noexcept = default;

bool JsonReader::beginObject(std::string_view name) {
    const Json* value = selectRead(impl_->root, impl_->stack, name);
    if (!value || !value->is_object()) return fail("JSON object is missing or invalid");
    impl_->stack.push_back(value);
    return true;
}
bool JsonReader::endObject() {
    if (impl_->stack.empty()) return fail("JSON object stack is empty");
    impl_->stack.pop_back();
    return true;
}
bool JsonReader::beginArray(std::string_view name, std::uint32_t& size) {
    const Json* value = selectRead(impl_->root, impl_->stack, name);
    if (!value || !value->is_array() || value->size() > maxCollectionSize()) {
        return fail("JSON array is missing or invalid");
    }
    size = static_cast<std::uint32_t>(value->size());
    impl_->stack.push_back(value);
    return true;
}
bool JsonReader::beginArrayElement(std::uint32_t index) {
    if (impl_->stack.empty() || !impl_->stack.back()->is_array() ||
        index >= impl_->stack.back()->size()) {
        return fail("JSON array index is invalid");
    }
    impl_->stack.push_back(&(*impl_->stack.back())[index]);
    return true;
}
bool JsonReader::endArrayElement() { return endObject(); }
bool JsonReader::endArray() { return endObject(); }

bool JsonReader::transferBool(std::string_view name, bool& value) {
    const Json* source = selectRead(impl_->root, impl_->stack, name);
    if (!source || !source->is_boolean()) return fail("JSON bool is missing or invalid");
    value = source->get<bool>();
    return true;
}
#define MINI_JSON_READ_SIGNED(Type, Name)                                      \
    bool JsonReader::Name(std::string_view name, Type& value) {                \
        if (!readSigned(selectRead(impl_->root, impl_->stack, name), value))   \
            return fail("JSON signed integer is missing or invalid");         \
        return true;                                                           \
    }
MINI_JSON_READ_SIGNED(std::int8_t, transferInt8)
MINI_JSON_READ_SIGNED(std::int16_t, transferInt16)
MINI_JSON_READ_SIGNED(std::int32_t, transferInt32)
MINI_JSON_READ_SIGNED(std::int64_t, transferInt64)
#undef MINI_JSON_READ_SIGNED
#define MINI_JSON_READ_UNSIGNED(Type, Name)                                    \
    bool JsonReader::Name(std::string_view name, Type& value) {                \
        if (!readUnsigned(selectRead(impl_->root, impl_->stack, name), value)) \
            return fail("JSON unsigned integer is missing or invalid");       \
        return true;                                                           \
    }
MINI_JSON_READ_UNSIGNED(std::uint8_t, transferUInt8)
MINI_JSON_READ_UNSIGNED(std::uint16_t, transferUInt16)
MINI_JSON_READ_UNSIGNED(std::uint32_t, transferUInt32)
MINI_JSON_READ_UNSIGNED(std::uint64_t, transferUInt64)
#undef MINI_JSON_READ_UNSIGNED

bool JsonReader::transferFloat(std::string_view name, float& value) {
    const Json* source = selectRead(impl_->root, impl_->stack, name);
    if (!source || !source->is_number()) return fail("JSON float is missing or invalid");
    value = source->get<float>();
    return std::isfinite(value) || fail("JSON float is not finite");
}
bool JsonReader::transferDouble(std::string_view name, double& value) {
    const Json* source = selectRead(impl_->root, impl_->stack, name);
    if (!source || !source->is_number()) return fail("JSON double is missing or invalid");
    value = source->get<double>();
    return std::isfinite(value) || fail("JSON double is not finite");
}
bool JsonReader::transferString(std::string_view name, std::string& value) {
    const Json* source = selectRead(impl_->root, impl_->stack, name);
    if (!source || !source->is_string()) return fail("JSON string is missing or invalid");
    value = source->get<std::string>();
    return true;
}
bool JsonReader::transferBytes(std::string_view name,
                               std::vector<std::byte>& value) {
    const Json* source = selectRead(impl_->root, impl_->stack, name);
    if (!source || !source->is_array() || source->size() > maxCollectionSize()) {
        return fail("JSON byte array is missing or invalid");
    }
    value.clear();
    value.reserve(source->size());
    for (const Json& element : *source) {
        std::uint8_t byte{};
        if (!readUnsigned(&element, byte)) return fail("JSON byte is invalid");
        value.push_back(static_cast<std::byte>(byte));
    }
    return true;
}

} // namespace engine
