#pragma once

// Shared JSON helpers for source-format parsing (.shader.json / .material.json /
// .scene.json). Each XXXAssetFormat unit in this directory owns one source format
// and composes these primitives; the runtime classes (Shader, Material, Scene)
// no longer carry source-file parsing.

#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "core/math/Math.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace engine::format {

using Json = nlohmann::json;

// Parsing abort signal: format units catch it and surface an empty result.
struct AssetParseFailure final {};

// Logs the failure and aborts parsing. category keeps the historical log channel
// of the calling format unit (e.g. "ShaderAsset").
[[noreturn]] inline void fail(const char* category,
                              const VirtualPath& file,
                              const std::string& path,
                              const std::string& message) {
    Log::error(category, "%s: %s: %s", file.string().c_str(), path.c_str(), message.c_str());
    throw AssetParseFailure{};
}

[[nodiscard]] inline Json
readJson(const char* category, const VirtualPath& file, std::string_view source) {
    try {
        return Json::parse(source.begin(), source.end());
    } catch (const Json::parse_error& error) {
        fail(category,
             file,
             "$",
             "JSON parse error at byte " + std::to_string(error.byte) + ": " + error.what());
    }
}

template <typename T>
[[nodiscard]] T required(const char* category,
                         const Json& object,
                         const char* key,
                         const VirtualPath& file,
                         const std::string& path) {
    if (!object.contains(key)) {
        fail(category, file, path, std::string("missing required field '") + key + "'");
    }
    try {
        return object.at(key).get<T>();
    } catch (const Json::exception&) {
        fail(category, file, path + "." + key, "invalid value type");
    }
}

template <typename Enum>
[[nodiscard]] Enum parseEnum(const char* category,
                             const std::string& value,
                             std::initializer_list<std::pair<const char*, Enum>> values,
                             const VirtualPath& file,
                             const std::string& path) {
    for (const auto& [name, result] : values) {
        if (value == name) {
            return result;
        }
    }
    fail(category, file, path, "unknown value '" + value + "'");
}

template <glm::length_t Length, typename Vector>
[[nodiscard]] Vector vectorValue(const char* category,
                                 const Json& value,
                                 const VirtualPath& file,
                                 const std::string& path) {
    if (!value.is_array() || value.size() != Length) {
        fail(category, file, path, "expected an array of " + std::to_string(Length) + " numbers");
    }
    try {
        Vector result{0.0F};
        for (glm::length_t i = 0; i < Length; ++i) {
            result[i] = value[i].get<float>();
        }
        return result;
    } catch (const Json::exception&) {
        fail(category, file, path, "vector elements must be numbers");
    }
}

} // namespace engine::format
