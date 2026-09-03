#include "renderer/Shader.h"

#include "core/Log.h"
#include "core/io/FileSystem.h"

#include <nlohmann/json.hpp>
#include <spirv_cross.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace engine {
namespace {

struct ReflectionFailure final {};

template <typename... Args>
[[noreturn]] void reflectionFail(const char* format, Args... args) {
    Log::error("SpirvReflection", format, args...);
    throw ReflectionFailure{};
}

using Json = nlohmann::json;

struct AssetParseFailure final {};

[[noreturn]] void fail(const VirtualPath& file, const std::string& path,
                       const std::string& message) {
    Log::error("ShaderAsset", "%s: %s: %s", file.string().c_str(), path.c_str(),
               message.c_str());
    throw AssetParseFailure{};
}

Json readJson(const VirtualPath& file, std::string_view source) {
    try {
        return Json::parse(source.begin(), source.end());
    } catch (const Json::parse_error& error) {
        fail(file, "$", "JSON parse error at byte " +
                            std::to_string(error.byte) + ": " + error.what());
    }
}

template <typename T>
T required(const Json& object, const char* key, const VirtualPath& file,
           const std::string& path) {
    if (!object.contains(key)) {
        fail(file, path, std::string("missing required field '") + key + "'");
    }
    try {
        return object.at(key).get<T>();
    } catch (const Json::exception&) {
        fail(file, path + "." + key, "invalid value type");
    }
}

template <typename Enum>
Enum parseEnum(const std::string& value,
               std::initializer_list<std::pair<const char*, Enum>> values,
               const VirtualPath& file, const std::string& path) {
    for (const auto& [name, result] : values) {
        if (value == name) {
            return result;
        }
    }
    fail(file, path, "unknown value '" + value + "'");
}

template <glm::length_t Length, typename Vector>
Vector vectorValue(const Json& value, const VirtualPath& file,
                   const std::string& path) {
    if (!value.is_array() || value.size() != Length) {
        fail(file, path, "expected an array of " + std::to_string(Length) + " numbers");
    }
    try {
        Vector result{0.0F};
        for (glm::length_t i = 0; i < Length; ++i) {
            result[i] = value[i].get<float>();
        }
        return result;
    } catch (const Json::exception&) {
        fail(file, path, "vector elements must be numbers");
    }
}

ShaderPropertyType propertyType(const std::string& value, const VirtualPath& file,
                                const std::string& path) {
    return parseEnum<ShaderPropertyType>(
        value,
        {{"Float", ShaderPropertyType::Float}, {"Range", ShaderPropertyType::Range},
         {"Vec2", ShaderPropertyType::Vec2}, {"Vec3", ShaderPropertyType::Vec3},
         {"Vec4", ShaderPropertyType::Vec4}, {"Vector", ShaderPropertyType::Vec4},
         {"Color", ShaderPropertyType::Color},
         {"Texture2D", ShaderPropertyType::Texture2D}, {"Bool", ShaderPropertyType::Boolean}},
        file, path);
}

ShaderValueType interfaceValueType(const std::string& value,
                                   const VirtualPath& file,
                                   const std::string& path) {
    return parseEnum<ShaderValueType>(
        value,
        {{"Float", ShaderValueType::Float}, {"Vec2", ShaderValueType::Vec2},
         {"Vec3", ShaderValueType::Vec3}, {"Vec4", ShaderValueType::Vec4}},
        file, path);
}

ShaderInterpolation interpolation(const std::string& value,
                                  const VirtualPath& file,
                                  const std::string& path) {
    return parseEnum<ShaderInterpolation>(
        value,
        {{"Smooth", ShaderInterpolation::Smooth}, {"Flat", ShaderInterpolation::Flat},
         {"NoPerspective", ShaderInterpolation::NoPerspective}},
        file, path);
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

std::vector<ShaderInterfaceVariable> parseInterfaceVariables(
    const Json& pass, const char* key, bool requireSemantic, bool allowInterpolation,
    const VirtualPath& file, const std::string& passPath) {
    if (!pass.contains(key)) {
        return {};
    }
    const Json& variables = pass.at(key);
    const std::string listPath = passPath + "." + key;
    if (!variables.is_array()) {
        fail(file, listPath, "must be an array");
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
            fail(file, at, "interface variable must be an object");
        }
        ShaderInterfaceVariable variable;
        variable.name = required<std::string>(json, "name", file, at);
        if (!validIdentifier(variable.name)) {
            fail(file, at + ".name", "must be a valid shader identifier");
        }
        if (!names.insert(variable.name).second) {
            fail(file, at + ".name", "duplicate interface name '" + variable.name + "'");
        }
        if (requireSemantic) {
            variable.semantic = required<std::string>(json, "semantic", file, at);
            if (variable.semantic.empty()) {
                fail(file, at + ".semantic", "must not be empty");
            }
            if (!semantics.insert(variable.semantic).second) {
                fail(file, at + ".semantic",
                     "duplicate vertex semantic '" + variable.semantic + "'");
            }
        } else {
            variable.semantic = json.value("semantic", std::string{});
        }
        variable.type = interfaceValueType(
            required<std::string>(json, "type", file, at), file, at + ".type");
        if (!json.contains("location") || !json.at("location").is_number_integer()) {
            fail(file, at + ".location", "must be a non-negative integer");
        }
        const std::int64_t location = json.at("location").get<std::int64_t>();
        if (location < 0 || location > std::numeric_limits<std::uint32_t>::max()) {
            fail(file, at + ".location", "must be a non-negative 32-bit integer");
        }
        variable.location = static_cast<std::uint32_t>(location);
        if (!locations.insert(variable.location).second) {
            fail(file, at + ".location",
                 "duplicate interface location " + std::to_string(variable.location));
        }
        if (json.contains("interpolation")) {
            if (!allowInterpolation) {
                fail(file, at + ".interpolation",
                     "interpolation is only valid for varyings");
            }
            variable.interpolation = interpolation(
                required<std::string>(json, "interpolation", file, at), file,
                at + ".interpolation");
        }
        result.push_back(std::move(variable));
    }
    return result;
}

ShaderValue parseValue(const Json& value, ShaderPropertyType type,
                       const VirtualPath& file, const std::string& path) {
    try {
        switch (type) {
        case ShaderPropertyType::Float:
        case ShaderPropertyType::Range:
            return value.get<float>();
        case ShaderPropertyType::Boolean:
            return value.get<bool>();
        case ShaderPropertyType::Vec2:
            return vectorValue<2, math::Vec2>(value, file, path);
        case ShaderPropertyType::Vec3:
            return vectorValue<3, math::Vec3>(value, file, path);
        case ShaderPropertyType::Vec4:
        case ShaderPropertyType::Color:
            return vectorValue<4, math::Vec4>(value, file, path);
        case ShaderPropertyType::Texture2D:
            return value.get<std::string>();
        }
    } catch (const Json::exception&) {
        fail(file, path, "value does not match property type");
    }
    fail(file, path, "unsupported property type");
}

ShaderValue parseMaterialValue(const Json& value, const VirtualPath& file,
                               const std::string& path) {
    try {
        if (value.is_number()) {
            return value.get<float>();
        }
        if (value.is_boolean()) {
            return value.get<bool>();
        }
        if (value.is_string()) {
            return value.get<std::string>();
        }
        if (value.is_array()) {
            switch (value.size()) {
            case 2:
                return vectorValue<2, math::Vec2>(value, file, path);
            case 3:
                return vectorValue<3, math::Vec3>(value, file, path);
            case 4:
                return vectorValue<4, math::Vec4>(value, file, path);
            default:
                break;
            }
        }
    } catch (const Json::exception&) {
        fail(file, path, "unsupported material property value");
    }
    fail(file, path, "unsupported material property value");
}

bool valueMatchesProperty(const ShaderValue& value, ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range:
        return std::holds_alternative<float>(value);
    case ShaderPropertyType::Boolean:
        return std::holds_alternative<bool>(value);
    case ShaderPropertyType::Vec2:
        return std::holds_alternative<math::Vec2>(value);
    case ShaderPropertyType::Vec3:
        return std::holds_alternative<math::Vec3>(value);
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color:
        return std::holds_alternative<math::Vec4>(value);
    case ShaderPropertyType::Texture2D:
        return std::holds_alternative<std::string>(value);
    }
    return false;
}

RenderStateDesc parseState(const Json& value, const VirtualPath& file,
                           const std::string& path) {
    RenderStateDesc state;
    if (value.contains("cull")) {
        state.cull = parseEnum<CullMode>(value.at("cull").get<std::string>(),
            {{"Off", CullMode::Off}, {"Front", CullMode::Front}, {"Back", CullMode::Back}},
            file, path + ".cull");
    }
    if (value.contains("frontFace")) {
        state.frontFace = parseEnum<FrontFace>(value.at("frontFace").get<std::string>(),
            {{"CW", FrontFace::Clockwise}, {"CCW", FrontFace::CounterClockwise}}, file,
            path + ".frontFace");
    }
    if (value.contains("fill")) {
        state.fill = parseEnum<FillMode>(value.at("fill").get<std::string>(),
            {{"Solid", FillMode::Solid}, {"Wireframe", FillMode::Wireframe}}, file,
            path + ".fill");
    }
    if (value.contains("topology")) {
        state.topology = parseEnum<PrimitiveTopology>(value.at("topology").get<std::string>(),
            {{"TriangleList", PrimitiveTopology::TriangleList},
             {"LineList", PrimitiveTopology::LineList}}, file, path + ".topology");
    }
    state.depthWrite = value.value("depthWrite", state.depthWrite);
    if (value.contains("depthTest")) {
        state.depthTest = parseEnum<DepthCompare>(value.at("depthTest").get<std::string>(),
            {{"Never", DepthCompare::Never}, {"Less", DepthCompare::Less},
             {"LessEqual", DepthCompare::LessEqual}, {"Equal", DepthCompare::Equal},
             {"Greater", DepthCompare::Greater},
             {"GreaterEqual", DepthCompare::GreaterEqual}, {"Always", DepthCompare::Always}},
            file, path + ".depthTest");
    }
    if (value.contains("blend")) {
        state.blend = parseEnum<BlendMode>(value.at("blend").get<std::string>(),
            {{"Off", BlendMode::Off}, {"Alpha", BlendMode::Alpha},
             {"Additive", BlendMode::Additive},
             {"PremultipliedAlpha", BlendMode::PremultipliedAlpha}}, file,
            path + ".blend");
    }
    state.colorMask = value.value("colorMask", state.colorMask);
    if (state.colorMask.find_first_not_of("RGBA") != std::string::npos) {
        fail(file, path + ".colorMask", "only R, G, B and A are allowed");
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
    const std::unordered_map<std::string, int> queues{{"Background", 1000}, {"Opaque", 2000},
                                                      {"AlphaTest", 2450}, {"Transparent", 3000},
                                                      {"Overlay", 4000}};
    const auto plus = value.find('+');
    const std::string base = value.substr(0, plus);
    const auto found = queues.find(base);
    if (found == queues.end()) {
        fail(file, "$.tags.queue", "unknown render queue '" + value + "'");
    }
    if (plus == std::string::npos) {
        return found->second;
    }
    try {
        return found->second + std::stoi(value.substr(plus + 1));
    } catch (...) {
        fail(file, "$.tags.queue", "invalid queue offset in '" + value + "'");
    }
}

} // namespace

ShaderKeywordSchema::ShaderKeywordSchema(std::vector<std::string> keywords)
    : keywords_(std::move(keywords)) {
    std::ranges::sort(keywords_);
    keywords_.erase(std::unique(keywords_.begin(), keywords_.end()),
                    keywords_.end());
    if (keywords_.size() > 64) {
        Log::fatal("ShaderKeywordSchema", "A pass cannot declare more than 64 keywords");
    }
}

bool ShaderKeywordSchema::declares(std::string_view keyword) const {
    return std::ranges::binary_search(keywords_, keyword);
}

ShaderVariantKey ShaderKeywordSchema::makeKey(
    std::span<const std::string> enabledKeywords,
    std::uint32_t meshFeatureBits,
    std::uint32_t platformFeatureBits) const {
    ShaderVariantKey result{0, meshFeatureBits, platformFeatureBits};
    for (const std::string& keyword : enabledKeywords) {
        const auto found = std::ranges::lower_bound(keywords_, keyword);
        if (found == keywords_.end() || *found != keyword) {
            Log::warn("ShaderKeywordSchema", "Keyword is not declared: %s",
                      keyword.c_str());
            continue;
        }
        const std::size_t bit =
            static_cast<std::size_t>(std::distance(keywords_.begin(), found));
        result.keywordBits |= std::uint64_t{1} << bit;
    }
    return result;
}

const ShaderPassDesc& SubShaderDesc::requirePass(ShaderPassType type) const {
    const auto found = std::ranges::find_if(passes, [type](const ShaderPassAsset& asset) {
        return asset.pass.type == type;
    });
    if (found == passes.end()) {
        Log::fatal("ShaderAsset", "SubShader has no required pass");
    }
    return found->pass;
}

const ShaderPropertyDesc* ShaderAsset::findProperty(const std::string& name) const {
    const auto found = std::ranges::find_if(properties, [&name](const ShaderPropertyDesc& property) {
        return property.name == name;
    });
    return found == properties.end() ? nullptr : &*found;
}

ShaderAsset parseShaderAssetValue(const VirtualPath& path,
                                  std::string_view source) {
    const Json root = readJson(path, source);
    if (!root.is_object()) {
        fail(path, "$", "shader asset root must be an object");
    }
    const int schemaVersion = required<int>(root, "$schemaVersion", path, "$");
    if (schemaVersion != 1) {
        fail(path, "$.$schemaVersion", "unsupported schema version " +
                                             std::to_string(schemaVersion));
    }
    ShaderAsset asset;
    asset.setAssetPath(path);
    asset.name = required<std::string>(root, "name", path, "$");
    const Json rootTags = root.value("tags", Json::object());

    std::set<std::string> propertyNames;
    const Json properties = root.value("properties", Json::array());
    for (std::size_t i = 0; i < properties.size(); ++i) {
        const Json& json = properties[i];
        const std::string at = "$.properties[" + std::to_string(i) + "]";
        ShaderPropertyDesc property;
        property.name = required<std::string>(json, "name", path, at);
        property.displayName = json.value("displayName", property.name);
        property.type = propertyType(required<std::string>(json, "type", path, at), path,
                                     at + ".type");
        if (!propertyNames.insert(property.name).second) {
            fail(path, at + ".name", "duplicate property '" + property.name + "'");
        }
        if (!json.contains("default")) {
            fail(path, at, "missing required field 'default'");
        }
        property.defaultValue = parseValue(json.at("default"), property.type, path, at + ".default");
        if (property.type == ShaderPropertyType::Range) {
            if (!json.contains("range") || !json.at("range").is_array() ||
                json.at("range").size() != 2) {
                fail(path, at + ".range", "Range property requires [min, max]");
            }
            property.range = math::Vec2{json.at("range")[0].get<float>(),
                                        json.at("range")[1].get<float>()};
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
        fail(path, "$", "a non-empty 'subShaders' array is required");
    }
    if (subShaders.empty()) {
        fail(path, "$.subShaders", "at least one SubShader is required");
    }
    for (std::size_t subShaderIndex = 0; subShaderIndex < subShaders.size();
         ++subShaderIndex) {
        const Json& subShaderJson = subShaders[subShaderIndex];
        const std::string subShaderAt =
            "$.subShaders[" + std::to_string(subShaderIndex) + "]";
        if (!subShaderJson.is_object()) {
            fail(path, subShaderAt, "must be an object");
        }
        SubShaderDesc subShader;
        const Json tags = subShaderJson.value("tags", rootTags);
        subShader.renderPipeline =
            tags.value("renderPipeline", subShader.renderPipeline);
        subShader.renderType = tags.value("renderType", subShader.renderType);
        subShader.renderQueue = parseQueue(tags, path);
        const Json passes = subShaderJson.value("passes", Json::array());
        if (passes.empty()) {
            fail(path, subShaderAt + ".passes", "at least one pass is required");
        }
        std::set<std::string> passNames;
        for (std::size_t i = 0; i < passes.size(); ++i) {
        const Json& json = passes[i];
        const std::string at = subShaderAt + ".passes[" + std::to_string(i) + "]";
        ShaderPassAsset passAsset;
        ShaderPassDesc& pass = passAsset.pass;
        pass.name = required<std::string>(json, "name", path, at);
        if (!passNames.insert(pass.name).second) {
            fail(path, at + ".name", "duplicate pass '" + pass.name + "'");
        }
        pass.type = parseEnum<ShaderPassType>(required<std::string>(json, "lightMode", path, at),
            {{"Forward", ShaderPassType::Forward}, {"DepthOnly", ShaderPassType::DepthOnly},
             {"ShadowCaster", ShaderPassType::ShadowCaster}}, path, at + ".lightMode");
        if (!json.contains("program") || !json.at("program").is_object()) {
            fail(path, at + ".program", "must be an object");
        }
        const Json& program = json.at("program");
        pass.program.vertexSource = path.parent().joined(
            required<std::string>(program, "vertex", path, at + ".program"));
        pass.program.fragmentSource = path.parent().joined(
            required<std::string>(program, "frag", path, at + ".program"));
        if (pass.program.vertexSource.extension() != ".vert") {
            fail(path, at + ".program.vertex", "must reference a .vert source file");
        }
        if (pass.program.fragmentSource.extension() != ".frag") {
            fail(path, at + ".program.frag", "must reference a .frag source file");
        }
        pass.vertexInput =
            parseInterfaceVariables(json, "vertexInput", true, false, path, at);
        pass.varyings =
            parseInterfaceVariables(json, "varyings", false, true, path, at);
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

MaterialAsset parseMaterialAssetValue(const VirtualPath& path,
                                      std::string_view source) {
    const Json root = readJson(path, source);
    if (!root.is_object()) {
        fail(path, "$", "material asset root must be an object");
    }
    const int schemaVersion = required<int>(root, "$schemaVersion", path, "$");
    if (schemaVersion != 1) {
        fail(path, "$.$schemaVersion", "unsupported schema version " +
                                             std::to_string(schemaVersion));
    }
    MaterialAsset material;
    material.setAssetPath(path);
    material.name = required<std::string>(root, "name", path, "$");
    const std::string shaderPath = required<std::string>(root, "shader", path, "$");
    material.shader = VirtualPath{shaderPath};
    if (!material.shader.valid()) {
        material.shader = VirtualPath{"asset://" + shaderPath};
    }
    if (!material.shader.valid()) {
        fail(path, "$.shader", "invalid Shader virtual path");
    }
    if (root.contains("renderQueue")) {
        material.renderQueue = root.at("renderQueue").get<int>();
    }
    material.keywords = root.value("keywords", std::vector<std::string>{});
    std::set<std::string> usedKeywords;
    for (const std::string& keyword : material.keywords) {
        if (!usedKeywords.insert(keyword).second) {
            fail(path, "$.keywords", "duplicate keyword '" + keyword + "'");
        }
    }
    const Json values = root.value("properties", Json::object());
    for (const auto& [name, value] : values.items()) {
        material.properties.emplace(
            name, parseMaterialValue(value, path, "$.properties." + name));
    }
    return material;
}

std::shared_ptr<ShaderAsset> detail::parseShaderAsset(
    const VirtualPath& path, std::string_view source) {
    try {
        return std::make_shared<ShaderAsset>(parseShaderAssetValue(path, source));
    } catch (const AssetParseFailure&) {
        return {};
    } catch (const Json::exception& error) {
        Log::error("ShaderAsset", "%s: JSON value error: %s",
                   path.string().c_str(), error.what());
        return {};
    }
}

std::shared_ptr<MaterialAsset> detail::parseMaterialAsset(
    const VirtualPath& path, std::string_view source) {
    try {
        return std::make_shared<MaterialAsset>(parseMaterialAssetValue(path, source));
    } catch (const AssetParseFailure&) {
        return {};
    } catch (const Json::exception& error) {
        Log::error("MaterialAsset", "%s: JSON value error: %s",
                   path.string().c_str(), error.what());
        return {};
    }
}

bool validateMaterialAsset(const MaterialAsset& material,
                           const ShaderAsset& shader,
                           const VirtualPath& materialPath) {
    bool valid = true;
    const auto report = [&](const std::string& path, const std::string& message) {
        valid = false;
        Log::error("MaterialAsset", "%s: %s: %s", materialPath.string().c_str(),
                   path.c_str(), message.c_str());
    };
    std::set<std::string> declaredKeywords;
    for (const SubShaderDesc& subShader : shader.subShaders) {
        for (const ShaderPassAsset& passAsset : subShader.passes) {
            const ShaderPassDesc& pass = passAsset.pass;
            declaredKeywords.insert(pass.features.begin(), pass.features.end());
        }
    }
    for (const std::string& keyword : material.keywords) {
        if (!declaredKeywords.contains(keyword)) {
            report("$.keywords", "keyword '" + keyword +
                                   "' is not declared by shader '" + shader.name + "'");
        }
    }
    for (const auto& [name, value] : material.properties) {
        const ShaderPropertyDesc* property = shader.findProperty(name);
        if (!property) {
            report("$.properties." + name,
                 "property is not declared by shader '" + shader.name + "'");
            continue;
        }
        if (!valueMatchesProperty(value, property->type)) {
            report("$.properties." + name, "value does not match property type");
        }
    }
    return valid;
}

} // namespace engine

namespace engine {
namespace {

struct Std140TypeLayout {
    std::uint32_t size;
    std::uint32_t alignment;
};

Std140TypeLayout std140TypeLayout(ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range:
    case ShaderPropertyType::Boolean:
        return {4, 4};
    case ShaderPropertyType::Vec2:
        return {8, 8};
    case ShaderPropertyType::Vec3:
        return {16, 16};
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color:
        return {16, 16};
    case ShaderPropertyType::Texture2D:
        assert(false && "Texture2D is a descriptor, not a uniform member");
    }
    Log::fatal("ShaderLayout", "Unsupported Shader property type");
}

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment) {
    const std::uint64_t aligned =
        (static_cast<std::uint64_t>(value) + alignment - 1) & ~(alignment - 1ULL);
    if (aligned > std::numeric_limits<std::uint32_t>::max()) {
        Log::fatal("ShaderLayout", "Material uniform layout exceeds 32-bit size");
    }
    return static_cast<std::uint32_t>(aligned);
}

} // namespace

const UniformMemberLayout* UniformBlockLayout::findMember(std::string_view name) const {
    const auto member = std::ranges::find_if(
        members, [name](const UniformMemberLayout& item) { return item.name == name; });
    return member == members.end() ? nullptr : &*member;
}

const UniformMemberLayout& UniformBlockLayout::requireMember(
    std::string_view name) const {
    if (const UniformMemberLayout* member = findMember(name)) {
        return *member;
    }
    Log::fatal("ShaderLayout", "Material uniform member does not exist: " +
                                   std::string{name});
}

bool isUniformProperty(ShaderPropertyType type) {
    return type != ShaderPropertyType::Texture2D;
}

UniformBlockLayout
buildUniformBlockLayout(std::span<const ShaderPropertyDesc> properties) {
    UniformBlockLayout result;
    std::uint32_t cursor = 0;
    for (const ShaderPropertyDesc& property : properties) {
        if (!isUniformProperty(property.type)) {
            continue;
        }
        const Std140TypeLayout typeLayout = std140TypeLayout(property.type);
        const std::uint32_t offset = alignUp(cursor, typeLayout.alignment);
        if (offset > std::numeric_limits<std::uint32_t>::max() - typeLayout.size) {
            Log::fatal("ShaderLayout", "Material uniform layout exceeds 32-bit size");
        }
        result.members.push_back(
            {property.name, property.type, offset, typeLayout.size, typeLayout.alignment});
        cursor = offset + typeLayout.size;
    }
    result.byteSize = result.members.empty() ? 0 : alignUp(cursor, 16);
    return result;
}

ShaderPass::ShaderPass(const ShaderPassDesc& desc,
                       const RenderStateDesc& renderState)
    : name_(desc.name),
      type_(desc.type),
      program_(desc.program),
      vertexInput_(desc.vertexInput),
      varyings_(desc.varyings),
      fragmentOutputs_(desc.fragmentOutputs),
      renderState_(renderState),
      features_(desc.features),
      keywordSchema_(desc.features) {}

ShaderVariantKey ShaderPass::variantKey(
    std::span<const std::string> materialKeywords,
    std::uint32_t meshFeatureBits,
    std::uint32_t platformFeatureBits) const {
    return keywordSchema_.makeKey(materialKeywords, meshFeatureBits,
                                  platformFeatureBits);
}

SubShader::SubShader(const SubShaderDesc& desc)
    : renderPipeline_(desc.renderPipeline),
      renderType_(desc.renderType),
      renderQueue_(desc.renderQueue) {
    passes_.reserve(desc.passes.size());
    for (const ShaderPassAsset& asset : desc.passes) {
        passes_.emplace_back(asset.pass, asset.renderState);
    }
}

const ShaderPass* SubShader::findPass(ShaderPassType type) const {
    const auto found = std::ranges::find_if(
        passes_, [type](const ShaderPass& pass) { return pass.type() == type; });
    return found == passes_.end() ? nullptr : &*found;
}

const ShaderPass& SubShader::requirePass(ShaderPassType type) const {
    if (const ShaderPass* pass = findPass(type)) {
        return *pass;
    }
    Log::fatal("SubShader", "Required pass does not exist");
}

bool SubShader::supports(std::string_view renderPipeline) const {
    return renderPipeline_ == renderPipeline;
}

Shader::Shader(const ShaderAsset& asset)
    : assetPath_(asset.assetPath()),
      name_(asset.name),
      properties_(asset.properties),
      uniformBlockLayout_(buildUniformBlockLayout(properties_)) {
    subShaders_.reserve(asset.subShaders.size());
    for (const SubShaderDesc& subShader : asset.subShaders) {
        subShaders_.emplace_back(subShader);
    }
}

const SubShader* Shader::selectSubShader(std::string_view renderPipeline) const {
    const auto found = std::ranges::find_if(
        subShaders_, [renderPipeline](const SubShader& subShader) {
            return subShader.supports(renderPipeline);
        });
    return found == subShaders_.end() ? nullptr : &*found;
}

const SubShader& Shader::requireSubShader(std::string_view renderPipeline) const {
    if (const SubShader* subShader = selectSubShader(renderPipeline)) {
        return *subShader;
    }
    Log::fatal("Shader", "No compatible SubShader for render pipeline: %.*s",
               static_cast<int>(renderPipeline.size()), renderPipeline.data());
}

const SubShader& Shader::defaultSubShader() const {
    if (subShaders_.empty()) {
        Log::fatal("Shader", "Shader has no SubShader: %s", name_.c_str());
    }
    return subShaders_.front();
}

bool Shader::declaresKeyword(std::string_view keyword) const {
    return std::ranges::any_of(subShaders_, [keyword](const SubShader& subShader) {
        return std::ranges::any_of(
            subShader.passes(), [keyword](const ShaderPass& pass) {
                return std::ranges::find(pass.features(), keyword) !=
                       pass.features().end();
            });
    });
}

namespace {

ShaderValueType reflectValueType(const spirv_cross::SPIRType& type,
                                 const VirtualPath& path) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        switch (type.vecsize) {
        case 1: return ShaderValueType::Float;
        case 2: return ShaderValueType::Vec2;
        case 3: return ShaderValueType::Vec3;
        case 4: return ShaderValueType::Vec4;
        default: break;
        }
    }
    reflectionFail("Unsupported interface type in: %s",
                   path.string().c_str());
}

std::string resourceName(const spirv_cross::Compiler& compiler,
                         const spirv_cross::Resource& resource) {
    return resource.name.empty() ? compiler.get_name(resource.id) : resource.name;
}

const ShaderStageVariable* findStageVariable(
    const std::vector<ShaderStageVariable>& variables, std::uint32_t location) {
    const auto found = std::ranges::find_if(
        variables, [location](const ShaderStageVariable& variable) {
            return variable.location == location;
        });
    return found == variables.end() ? nullptr : &*found;
}

void validateInterface(const std::vector<ShaderInterfaceVariable>& expected,
                       const std::vector<ShaderStageVariable>& reflected,
                       const VirtualPath& path,
                       std::string_view interfaceName) {
    if (expected.size() != reflected.size()) {
        reflectionFail("%s %.*s count does not match ShaderLab declaration",
                       path.string().c_str(), static_cast<int>(interfaceName.size()),
                       interfaceName.data());
    }
    for (const ShaderInterfaceVariable& declared : expected) {
        const ShaderStageVariable* actual =
            findStageVariable(reflected, declared.location);
        if (!actual || actual->type != declared.type) {
            reflectionFail(
                "%s %.*s location %u does not match ShaderLab declaration",
                path.string().c_str(), static_cast<int>(interfaceName.size()),
                interfaceName.data(), declared.location);
        }
    }
}

const ShaderDescriptorBinding* findDescriptor(
    const SpirvReflection& reflection, std::uint32_t set,
    std::uint32_t binding) {
    const auto found = std::ranges::find_if(
        reflection.descriptors,
        [set, binding](const ShaderDescriptorBinding& descriptor) {
            return descriptor.set == set && descriptor.binding == binding;
        });
    return found == reflection.descriptors.end() ? nullptr : &*found;
}

bool validateMaterialBlock(const ShaderAsset& shader,
                           const SpirvReflection& reflection,
                           const VirtualPath& path) {
    const UniformBlockLayout layout = buildUniformBlockLayout(shader.properties);
    if (layout.members.empty()) {
        return true;
    }
    const ShaderDescriptorBinding* block = findDescriptor(reflection, 1, 0);
    if (!block) {
        return false;
    }
    if (block->type != ShaderDescriptorType::UniformBuffer) {
        reflectionFail("%s set 1 binding 0 is not a uniform block",
                       path.string().c_str());
    }
    for (const UniformMemberLayout& expected : layout.members) {
        const auto member = std::ranges::find_if(
            block->members, [&expected](const ShaderUniformMember& actual) {
                return actual.name == expected.name;
            });
        if (member == block->members.end() || member->offset != expected.offset) {
            reflectionFail("%s uniform member %s has an unexpected offset",
                           path.string().c_str(), expected.name.c_str());
        }
    }
    return true;
}

void validateTextureBindings(const ShaderAsset& shader,
                             const SpirvReflection& vertex,
                             const SpirvReflection& fragment) {
    std::uint32_t binding = 1;
    for (const ShaderPropertyDesc& property : shader.properties) {
        if (property.type != ShaderPropertyType::Texture2D) {
            continue;
        }
        const ShaderDescriptorBinding* descriptor = findDescriptor(fragment, 1, binding);
        if (!descriptor) {
            descriptor = findDescriptor(vertex, 1, binding);
        }
        if (!descriptor ||
            descriptor->type != ShaderDescriptorType::CombinedImageSampler) {
            reflectionFail(
                "Texture property %s is missing descriptor set 1 binding %u",
                property.name.c_str(), binding);
        }
        ++binding;
    }
}

} // namespace

SpirvReflection reflectSpirvValue(const VirtualPath& path) {
    const auto bytes = FILE_SYSTEM.readBinary(path);
    if (!bytes) {
        reflectionFail("Cannot open SPIR-V: %s", path.string().c_str());
    }
    if (bytes->size() < 20 || bytes->size() % 4 != 0) {
        reflectionFail("Invalid SPIR-V byte count: %s", path.string().c_str());
    }
    std::vector<std::uint32_t> words(bytes->size() / 4U);
    std::memcpy(words.data(), bytes->data(), bytes->size());
    if (words[0] != 0x07230203U) {
        reflectionFail("Invalid SPIR-V magic: %s", path.string().c_str());
    }

    try {
        spirv_cross::Compiler compiler(std::move(words));
        const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        SpirvReflection result;

        switch (compiler.get_execution_model()) {
        case spv::ExecutionModelVertex: result.stage = ShaderStage::Vertex; break;
        case spv::ExecutionModelFragment: result.stage = ShaderStage::Fragment; break;
        default:
            reflectionFail("Unsupported shader stage: %s", path.string().c_str());
        }

        const auto reflectInterface =
            [&compiler, &path](const auto& stageResources) {
                std::vector<ShaderStageVariable> variables;
                variables.reserve(stageResources.size());
                for (const spirv_cross::Resource& resource : stageResources) {
                    if (compiler.has_decoration(resource.id, spv::DecorationBuiltIn) ||
                        !compiler.has_decoration(resource.id, spv::DecorationLocation)) {
                        continue;
                    }
                    variables.push_back(
                        {resourceName(compiler, resource),
                         reflectValueType(compiler.get_type(resource.type_id), path),
                         compiler.get_decoration(resource.id, spv::DecorationLocation)});
                }
                std::ranges::sort(
                    variables, [](const ShaderStageVariable& left,
                                  const ShaderStageVariable& right) {
                        return left.location < right.location;
                    });
                return variables;
            };
        result.inputs = reflectInterface(resources.stage_inputs);
        result.outputs = reflectInterface(resources.stage_outputs);

        const auto reflectDescriptors =
            [&compiler, &result](const auto& shaderResources,
                                 ShaderDescriptorType descriptorType,
                                 bool reflectMembers = false) {
                for (const spirv_cross::Resource& resource : shaderResources) {
                    if (!compiler.has_decoration(resource.id,
                                                 spv::DecorationDescriptorSet) ||
                        !compiler.has_decoration(resource.id,
                                                 spv::DecorationBinding)) {
                        continue;
                    }
                    ShaderDescriptorBinding descriptor;
                    descriptor.name = resourceName(compiler, resource);
                    descriptor.type = descriptorType;
                    descriptor.set = compiler.get_decoration(
                        resource.id, spv::DecorationDescriptorSet);
                    descriptor.binding = compiler.get_decoration(
                        resource.id, spv::DecorationBinding);
                    if (reflectMembers) {
                        const spirv_cross::SPIRType& blockType =
                            compiler.get_type(resource.base_type_id);
                        descriptor.members.reserve(blockType.member_types.size());
                        for (std::uint32_t member = 0;
                             member < blockType.member_types.size(); ++member) {
                            descriptor.members.push_back(
                                {compiler.get_member_name(resource.base_type_id, member),
                                 compiler.type_struct_member_offset(blockType, member)});
                        }
                    }
                    result.descriptors.push_back(std::move(descriptor));
                }
            };

        reflectDescriptors(resources.uniform_buffers,
                           ShaderDescriptorType::UniformBuffer, true);
        reflectDescriptors(resources.storage_buffers,
                           ShaderDescriptorType::StorageBuffer, true);
        reflectDescriptors(resources.sampled_images,
                           ShaderDescriptorType::CombinedImageSampler);
        reflectDescriptors(resources.separate_images,
                           ShaderDescriptorType::SampledImage);
        reflectDescriptors(resources.storage_images,
                           ShaderDescriptorType::SampledImage);
        reflectDescriptors(resources.subpass_inputs,
                           ShaderDescriptorType::SampledImage);
        reflectDescriptors(resources.separate_samplers,
                           ShaderDescriptorType::Sampler);
        std::ranges::sort(
            result.descriptors,
            [](const ShaderDescriptorBinding& left,
               const ShaderDescriptorBinding& right) {
                return std::tie(left.set, left.binding) <
                       std::tie(right.set, right.binding);
            });
        return result;
    } catch (const spirv_cross::CompilerError& error) {
        reflectionFail("Cannot reflect %s: %s", path.string().c_str(),
                       error.what());
    }
}

std::shared_ptr<SpirvReflection> reflectSpirv(
    const VirtualPath& path) {
    try {
        return std::make_shared<SpirvReflection>(reflectSpirvValue(path));
    } catch (const ReflectionFailure&) {
        return {};
    }
}

bool validateSpirvReflection(const ShaderAsset& shader,
                             const ShaderPassDesc& pass,
                             const VirtualPath& vertexSpirv,
                             const VirtualPath& fragmentSpirv) {
    const std::shared_ptr<SpirvReflection> vertexResult = reflectSpirv(vertexSpirv);
    const std::shared_ptr<SpirvReflection> fragmentResult = reflectSpirv(fragmentSpirv);
    if (!vertexResult || !fragmentResult) return false;
    const SpirvReflection& vertex = *vertexResult;
    const SpirvReflection& fragment = *fragmentResult;
    try {
    if (vertex.stage != ShaderStage::Vertex ||
        fragment.stage != ShaderStage::Fragment) {
        reflectionFail("SPIR-V stage does not match pass declaration");
    }
    validateInterface(pass.vertexInput, vertex.inputs, vertexSpirv, "vertex input");
    validateInterface(pass.varyings, vertex.outputs, vertexSpirv, "stage output");
    validateInterface(pass.varyings, fragment.inputs, fragmentSpirv, "stage input");
    validateInterface(pass.fragmentOutputs, fragment.outputs, fragmentSpirv,
                      "fragment output");
    const bool vertexHasMaterialBlock =
        validateMaterialBlock(shader, vertex, vertexSpirv);
    const bool fragmentHasMaterialBlock =
        validateMaterialBlock(shader, fragment, fragmentSpirv);
    if (!vertexHasMaterialBlock && !fragmentHasMaterialBlock &&
        !buildUniformBlockLayout(shader.properties).members.empty()) {
        reflectionFail("Material uniform block is absent from both shader stages");
    }
    validateTextureBindings(shader, vertex, fragment);

    for (const ShaderDescriptorBinding& descriptor : vertex.descriptors) {
        if (const ShaderDescriptorBinding* other =
                findDescriptor(fragment, descriptor.set, descriptor.binding);
            other && other->type != descriptor.type) {
            reflectionFail(
                "Descriptor type differs between vertex and fragment stages");
        }
    }
    } catch (const ReflectionFailure&) {
        return false;
    }
    return true;
}

} // namespace engine
