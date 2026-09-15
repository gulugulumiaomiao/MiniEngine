#include "asset/format/ShaderAssetFormat.h"

#include "asset/format/AssetFormatJson.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace engine::format {
namespace {

constexpr const char* kCategory = "ShaderAsset";

ShaderPropertyType
propertyType(const std::string& value, const VirtualPath& file, const std::string& path) {
    return parseEnum<ShaderPropertyType>(kCategory,
                                         value,
                                         {{"Float", ShaderPropertyType::Float},
                                          {"Range", ShaderPropertyType::Range},
                                          {"Vec2", ShaderPropertyType::Vec2},
                                          {"Vec3", ShaderPropertyType::Vec3},
                                          {"Vec4", ShaderPropertyType::Vec4},
                                          {"Vector", ShaderPropertyType::Vec4},
                                          {"Color", ShaderPropertyType::Color},
                                          {"Texture2D", ShaderPropertyType::Texture2D},
                                          {"Bool", ShaderPropertyType::Boolean}},
                                         file,
                                         path);
}

ShaderValueType
interfaceValueType(const std::string& value, const VirtualPath& file, const std::string& path) {
    return parseEnum<ShaderValueType>(kCategory,
                                      value,
                                      {{"Float", ShaderValueType::Float},
                                       {"Vec2", ShaderValueType::Vec2},
                                       {"Vec3", ShaderValueType::Vec3},
                                       {"Vec4", ShaderValueType::Vec4}},
                                      file,
                                      path);
}

ShaderInterpolation
interpolation(const std::string& value, const VirtualPath& file, const std::string& path) {
    return parseEnum<ShaderInterpolation>(kCategory,
                                          value,
                                          {{"Smooth", ShaderInterpolation::Smooth},
                                           {"Flat", ShaderInterpolation::Flat},
                                           {"NoPerspective", ShaderInterpolation::NoPerspective}},
                                          file,
                                          path);
}

bool validIdentifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const auto identifierCharacter = [](char character) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        return std::isalnum(unsignedCharacter) != 0 || character == '_';
    };
    const auto first = static_cast<unsigned char>(value.front());
    return (std::isalpha(first) != 0 || value.front() == '_') &&
           std::ranges::all_of(value, identifierCharacter);
}

std::vector<ShaderInterfaceVariable> parseInterfaceVariables(const Json& pass,
                                                             const char* key,
                                                             bool requireSemantic,
                                                             bool allowInterpolation,
                                                             const VirtualPath& file,
                                                             const std::string& passPath) {
    if (!pass.contains(key)) {
        return {};
    }
    const Json& variables = pass.at(key);
    const std::string listPath = passPath + "." + key;
    if (!variables.is_array()) {
        fail(kCategory, file, listPath, "must be an array");
    }

    std::set<std::string> names;
    std::set<std::uint32_t> locations;
    std::set<std::string> semantics;
    std::vector<ShaderInterfaceVariable> result;
    result.reserve(variables.size());
    for (std::size_t i = 0; i < variables.size(); ++i) {
        const Json& json = variables[i];
        const std::string at = listPath + "[" + std::to_string(i) + "]";
        if (!json.is_object()) {
            fail(kCategory, file, at, "interface variable must be an object");
        }
        ShaderInterfaceVariable variable;
        variable.name = required<std::string>(kCategory, json, "name", file, at);
        if (!validIdentifier(variable.name)) {
            fail(kCategory, file, at + ".name", "must be a valid shader identifier");
        }
        if (!names.insert(variable.name).second) {
            fail(kCategory, file, at + ".name", "duplicate interface name '" + variable.name + "'");
        }
        if (requireSemantic) {
            variable.semantic = required<std::string>(kCategory, json, "semantic", file, at);
            if (variable.semantic.empty()) {
                fail(kCategory, file, at + ".semantic", "must not be empty");
            }
            if (!semantics.insert(variable.semantic).second) {
                fail(kCategory,
                     file,
                     at + ".semantic",
                     "duplicate vertex semantic '" + variable.semantic + "'");
            }
        } else {
            variable.semantic = json.value("semantic", std::string{});
        }
        variable.type = interfaceValueType(
            required<std::string>(kCategory, json, "type", file, at), file, at + ".type");
        if (!json.contains("location") || !json.at("location").is_number_integer()) {
            fail(kCategory, file, at + ".location", "must be a non-negative integer");
        }
        const std::int64_t location = json.at("location").get<std::int64_t>();
        if (location < 0 || location > std::numeric_limits<std::uint32_t>::max()) {
            fail(kCategory, file, at + ".location", "must be a non-negative 32-bit integer");
        }
        variable.location = static_cast<std::uint32_t>(location);
        if (!locations.insert(variable.location).second) {
            fail(kCategory,
                 file,
                 at + ".location",
                 "duplicate interface location " + std::to_string(variable.location));
        }
        if (json.contains("interpolation")) {
            if (!allowInterpolation) {
                fail(kCategory, file, at + ".interpolation", "interpolation is only valid for varyings");
            }
            variable.interpolation =
                interpolation(required<std::string>(kCategory, json, "interpolation", file, at),
                              file,
                              at + ".interpolation");
        }
        result.push_back(std::move(variable));
    }
    return result;
}

ShaderValue parseValue(const Json& value,
                       ShaderPropertyType type,
                       const VirtualPath& file,
                       const std::string& path) {
    try {
        switch (type) {
        case ShaderPropertyType::Float:
        case ShaderPropertyType::Range: return value.get<float>();
        case ShaderPropertyType::Boolean: return value.get<bool>();
        case ShaderPropertyType::Vec2: return vectorValue<2, math::Vec2>(kCategory, value, file, path);
        case ShaderPropertyType::Vec3: return vectorValue<3, math::Vec3>(kCategory, value, file, path);
        case ShaderPropertyType::Vec4:
        case ShaderPropertyType::Color: return vectorValue<4, math::Vec4>(kCategory, value, file, path);
        case ShaderPropertyType::Texture2D: return value.get<std::string>();
        }
    } catch (const Json::exception&) {
        fail(kCategory, file, path, "value does not match property type");
    }
    fail(kCategory, file, path, "unsupported property type");
}

RenderStateDesc parseState(const Json& value, const VirtualPath& file, const std::string& path) {
    RenderStateDesc state;
    if (value.contains("cull")) {
        state.cull = parseEnum<CullMode>(kCategory,
                                         value.at("cull").get<std::string>(),
                                         {{"Off", CullMode::Off},
                                          {"Front", CullMode::Front},
                                          {"Back", CullMode::Back}},
                                         file,
                                         path + ".cull");
    }
    if (value.contains("frontFace")) {
        state.frontFace = parseEnum<FrontFace>(kCategory,
                                               value.at("frontFace").get<std::string>(),
                                               {{"CW", FrontFace::Clockwise},
                                                {"CCW", FrontFace::CounterClockwise}},
                                               file,
                                               path + ".frontFace");
    }
    if (value.contains("fill")) {
        state.fill = parseEnum<FillMode>(kCategory,
                                         value.at("fill").get<std::string>(),
                                         {{"Solid", FillMode::Solid},
                                          {"Wireframe", FillMode::Wireframe}},
                                         file,
                                         path + ".fill");
    }
    if (value.contains("topology")) {
        state.topology = parseEnum<PrimitiveTopology>(kCategory,
                                                      value.at("topology").get<std::string>(),
                                                      {{"TriangleList",
                                                        PrimitiveTopology::TriangleList},
                                                       {"LineList", PrimitiveTopology::LineList}},
                                                      file,
                                                      path + ".topology");
    }
    state.depthWrite = value.value("depthWrite", state.depthWrite);
    if (value.contains("depthTest")) {
        state.depthTest = parseEnum<DepthCompare>(kCategory,
                                                  value.at("depthTest").get<std::string>(),
                                                  {{"Never", DepthCompare::Never},
                                                   {"Less", DepthCompare::Less},
                                                   {"LessEqual", DepthCompare::LessEqual},
                                                   {"Equal", DepthCompare::Equal},
                                                   {"Greater", DepthCompare::Greater},
                                                   {"GreaterEqual", DepthCompare::GreaterEqual},
                                                   {"Always", DepthCompare::Always}},
                                                  file,
                                                  path + ".depthTest");
    }
    if (value.contains("blend")) {
        state.blend = parseEnum<BlendMode>(kCategory,
                                           value.at("blend").get<std::string>(),
                                           {{"Off", BlendMode::Off},
                                            {"Alpha", BlendMode::Alpha},
                                            {"Additive", BlendMode::Additive},
                                            {"PremultipliedAlpha", BlendMode::PremultipliedAlpha}},
                                           file,
                                           path + ".blend");
    }
    state.colorMask = value.value("colorMask", state.colorMask);
    if (state.colorMask.find_first_not_of("RGBA") != std::string::npos) {
        fail(kCategory, file, path + ".colorMask", "only R, G, B and A are allowed");
    }
    return state;
}

int parseQueue(const Json& tags, const VirtualPath& file) {
    if (!tags.contains("queue")) {
        return 2000;
    }
    if (tags.at("queue").is_number_integer()) {
        return tags.at("queue").get<int>();
    }
    const std::string value = tags.at("queue").get<std::string>();
    const std::unordered_map<std::string, int> queues{{"Background", 1000},
                                                      {"Opaque", 2000},
                                                      {"AlphaTest", 2450},
                                                      {"Transparent", 3000},
                                                      {"Overlay", 4000}};
    const auto plus = value.find('+');
    const std::string base = value.substr(0, plus);
    const auto found = queues.find(base);
    if (found == queues.end()) {
        fail(kCategory, file, "$.tags.queue", "unknown render queue '" + value + "'");
    }
    if (plus == std::string::npos) {
        return found->second;
    }
    try {
        return found->second + std::stoi(value.substr(plus + 1));
    } catch (...) {
        fail(kCategory, file, "$.tags.queue", "invalid queue offset in '" + value + "'");
    }
}

ShaderAsset parseShaderAssetValue(const VirtualPath& path, std::string_view source) {
    const Json root = readJson(kCategory, path, source);
    if (!root.is_object()) {
        fail(kCategory, path, "$", "shader asset root must be an object");
    }
    const int schemaVersion = required<int>(kCategory, root, "$schemaVersion", path, "$");
    if (schemaVersion != 1) {
        fail(kCategory,
             path,
             "$.$schemaVersion",
             "unsupported schema version " + std::to_string(schemaVersion));
    }
    ShaderAsset asset;
    asset.setAssetPath(path);
    asset.name = required<std::string>(kCategory, root, "name", path, "$");
    const Json rootTags = root.value("tags", Json::object());

    std::set<std::string> propertyNames;
    const Json properties = root.value("properties", Json::array());
    for (std::size_t i = 0; i < properties.size(); ++i) {
        const Json& json = properties[i];
        const std::string at = "$.properties[" + std::to_string(i) + "]";
        ShaderPropertyDesc property;
        property.name = required<std::string>(kCategory, json, "name", path, at);
        property.displayName = json.value("displayName", property.name);
        property.type = propertyType(
            required<std::string>(kCategory, json, "type", path, at), path, at + ".type");
        if (!propertyNames.insert(property.name).second) {
            fail(kCategory, path, at + ".name", "duplicate property '" + property.name + "'");
        }
        if (!json.contains("default")) {
            fail(kCategory, path, at, "missing required field 'default'");
        }
        property.defaultValue =
            parseValue(json.at("default"), property.type, path, at + ".default");
        if (property.type == ShaderPropertyType::Range) {
            if (!json.contains("range") || !json.at("range").is_array() ||
                json.at("range").size() != 2) {
                fail(kCategory, path, at + ".range", "Range property requires [min, max]");
            }
            property.range =
                math::Vec2{json.at("range")[0].get<float>(), json.at("range")[1].get<float>()};
        }
        property.attributes = json.value("attributes", std::vector<std::string>{});
        asset.properties.push_back(std::move(property));
    }

    Json subShaders;
    if (root.contains("subShaders") && root.at("subShaders").is_array()) {
        subShaders = root.at("subShaders");
    } else if (root.contains("subShader") && root.at("subShader").is_object()) {
        subShaders = Json::array({root.at("subShader")});
    } else {
        fail(kCategory, path, "$", "a non-empty 'subShaders' array is required");
    }
    if (subShaders.empty()) {
        fail(kCategory, path, "$.subShaders", "at least one SubShader is required");
    }
    for (std::size_t subShaderIndex = 0; subShaderIndex < subShaders.size(); ++subShaderIndex) {
        const Json& subShaderJson = subShaders[subShaderIndex];
        const std::string subShaderAt = "$.subShaders[" + std::to_string(subShaderIndex) + "]";
        if (!subShaderJson.is_object()) {
            fail(kCategory, path, subShaderAt, "must be an object");
        }
        SubShaderDesc subShader;
        const Json tags = subShaderJson.value("tags", rootTags);
        subShader.renderPipeline = tags.value("renderPipeline", subShader.renderPipeline);
        subShader.renderQueue = parseQueue(tags, path);
        const Json passes = subShaderJson.value("passes", Json::array());
        if (passes.empty()) {
            fail(kCategory, path, subShaderAt + ".passes", "at least one pass is required");
        }
        std::set<std::string> passNames;
        for (std::size_t i = 0; i < passes.size(); ++i) {
            const Json& json = passes[i];
            const std::string at = subShaderAt + ".passes[" + std::to_string(i) + "]";
            ShaderPassAsset passAsset;
            ShaderPassDesc& pass = passAsset.pass;
            pass.name = required<std::string>(kCategory, json, "name", path, at);
            if (!passNames.insert(pass.name).second) {
                fail(kCategory, path, at + ".name", "duplicate pass '" + pass.name + "'");
            }
            // lightMode is optional and defaults to the pass name, which for the
            // built-in pass names (Forward/DepthOnly/ShadowCaster) is the mode itself.
            pass.type =
                parseEnum<ShaderPassType>(kCategory,
                                          json.value("lightMode", pass.name),
                                          {{"Forward", ShaderPassType::Forward},
                                           {"DepthOnly", ShaderPassType::DepthOnly},
                                           {"ShadowCaster", ShaderPassType::ShadowCaster}},
                                          path,
                                          at + ".lightMode");
            if (!json.contains("program") || !json.at("program").is_object()) {
                fail(kCategory, path, at + ".program", "must be an object");
            }
            const Json& program = json.at("program");
            pass.program.vertexSource = path.parent().joined(
                required<std::string>(kCategory, program, "vertex", path, at + ".program"));
            pass.program.fragmentSource =
                path.parent().joined(
                    required<std::string>(kCategory, program, "frag", path, at + ".program"));
            if (pass.program.vertexSource.extension() != ".vert") {
                fail(kCategory, path, at + ".program.vertex", "must reference a .vert source file");
            }
            if (pass.program.fragmentSource.extension() != ".frag") {
                fail(kCategory, path, at + ".program.frag", "must reference a .frag source file");
            }
            pass.vertexInput = parseInterfaceVariables(json, "vertexInput", true, false, path, at);
            pass.varyings = parseInterfaceVariables(json, "varyings", false, true, path, at);
            pass.fragmentOutputs =
                parseInterfaceVariables(json, "fragmentOutputs", false, false, path, at);
            passAsset.renderState =
                parseState(json.value("state", Json::object()), path, at + ".state");
            pass.features = json.value("features", std::vector<std::string>{});
            subShader.passes.push_back(std::move(passAsset));
        }
        asset.subShaders.push_back(std::move(subShader));
    }
    return asset;
}

} // namespace

std::shared_ptr<ShaderAsset> parseShaderAsset(const VirtualPath& path, std::string_view source) {
    try {
        return std::make_shared<ShaderAsset>(parseShaderAssetValue(path, source));
    } catch (const AssetParseFailure&) {
        return {};
    } catch (const Json::exception& error) {
        Log::error("ShaderAsset", "%s: JSON value error: %s", path.string().c_str(), error.what());
        return {};
    }
}

} // namespace engine::format
