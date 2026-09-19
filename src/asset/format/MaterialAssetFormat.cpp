#include "asset/format/MaterialAssetFormat.h"

#include "asset/base/AssetReference.h"
#include "asset/format/AssetFormatJson.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace engine::format {
namespace {

constexpr const char* kCategory = "MaterialAsset";

using OrderedJson = nlohmann::ordered_json;

ShaderValue parseMaterialValue(const Json& value, const VirtualPath& file, const std::string& path) {
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
            case 2: return vectorValue<2, math::Vec2>(kCategory, value, file, path);
            case 3: return vectorValue<3, math::Vec3>(kCategory, value, file, path);
            case 4: return vectorValue<4, math::Vec4>(kCategory, value, file, path);
            default: break;
            }
        }
    } catch (const Json::exception&) {
        fail(kCategory, file, path, "unsupported material property value");
    }
    fail(kCategory, file, path, "unsupported material property value");
}

bool valueMatchesProperty(const ShaderValue& value, ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
    case ShaderPropertyType::Range: return std::holds_alternative<float>(value);
    case ShaderPropertyType::Boolean: return std::holds_alternative<bool>(value);
    case ShaderPropertyType::Vec2: return std::holds_alternative<math::Vec2>(value);
    case ShaderPropertyType::Vec3: return std::holds_alternative<math::Vec3>(value);
    case ShaderPropertyType::Vec4:
    case ShaderPropertyType::Color: return std::holds_alternative<math::Vec4>(value);
    case ShaderPropertyType::Texture2D: return std::holds_alternative<std::string>(value);
    }
    return false;
}

MaterialAsset parseMaterialAssetValue(const VirtualPath& path,
                                      std::string_view source,
                                      const GuidResolver* resolver) {
    const Json root = readJson(kCategory, path, source);
    if (!root.is_object()) {
        fail(kCategory, path, "$", "material asset root must be an object");
    }
    const int schemaVersion = required<int>(kCategory, root, "$schemaVersion", path, "$");
    if (schemaVersion != 1) {
        fail(kCategory,
             path,
             "$.$schemaVersion",
             "unsupported schema version " + std::to_string(schemaVersion));
    }
    MaterialAsset material;
    material.setAssetPath(path);
    material.name = required<std::string>(kCategory, root, "name", path, "$");
    const std::string shaderPath = required<std::string>(kCategory, root, "shader", path, "$");
    const AssetReference shaderRef{shaderPath};
    if (shaderRef.isGuid()) {
        if (!resolver) {
            fail(kCategory, path, "$.shader", "GUID shader reference requires a resolver");
        }
        const auto resolved = shaderRef.resolve(*resolver);
        if (!resolved) {
            fail(kCategory, path, "$.shader", "cannot resolve GUID shader reference: " + shaderPath);
        }
        material.shader = *resolved;
    } else if (shaderRef.isPath()) {
        material.shader = shaderRef.path();
    }
    if (!material.shader.valid()) {
        // A reference without a scheme resolves inside the mount that owns the material,
        // so the built-in Error Material under assets:// finds the built-in Shader in the
        // same project assets instead of looking for it in the currently mounted project.
        material.shader = VirtualPath{path.scheme() + "://" + shaderPath};
    }
    if (!material.shader.valid()) {
        fail(kCategory, path, "$.shader", "invalid Shader virtual path");
    }
    if (root.contains("renderQueue")) {
        material.renderQueue = root.at("renderQueue").get<int>();
    }
    const bool staticBatch = root.value("static_batch", false);
    const bool gpuInstancing = root.value("gpu_instancing", false);
    if (staticBatch && gpuInstancing) {
        fail(kCategory,
             path,
             "$",
             "static_batch and gpu_instancing are mutually exclusive");
    }
    material.batchingMode = staticBatch ? MaterialBatchingMode::Static
                            : gpuInstancing ? MaterialBatchingMode::GpuInstancing
                                            : MaterialBatchingMode::None;
    material.keywords = root.value("keywords", std::vector<std::string>{});
    std::set<std::string> usedKeywords;
    for (const std::string& keyword : material.keywords) {
        if (!usedKeywords.insert(keyword).second) {
            fail(kCategory, path, "$.keywords", "duplicate keyword '" + keyword + "'");
        }
    }
    const Json values = root.value("properties", Json::object());
    for (const auto& [name, value] : values.items()) {
        ShaderValue parsed = parseMaterialValue(value, path, "$.properties." + name);
        if (std::string* textureReference = std::get_if<std::string>(&parsed)) {
            const AssetReference textureRef{*textureReference};
            if (textureRef.isGuid()) {
                if (!resolver) {
                    fail(kCategory,
                         path,
                         "$.properties." + name,
                         "GUID texture reference requires a resolver");
                }
                const auto resolved = textureRef.resolve(*resolver);
                if (!resolved) {
                    fail(kCategory,
                         path,
                         "$.properties." + name,
                         "cannot resolve GUID texture reference: " + *textureReference);
                }
                *textureReference = resolved->string();
            } else if (textureRef.isPath()) {
                *textureReference = textureRef.path().string();
            }
        }
        material.properties.emplace(name, std::move(parsed));
    }
    return material;
}

} // namespace

std::shared_ptr<MaterialAsset> parseMaterialAsset(const VirtualPath& path, std::string_view source) {
    try {
        return std::make_shared<MaterialAsset>(parseMaterialAssetValue(path, source, nullptr));
    } catch (const AssetParseFailure&) {
        return {};
    } catch (const Json::exception& error) {
        Log::error("MaterialAsset", "%s: JSON value error: %s", path.string().c_str(), error.what());
        return {};
    }
}

std::shared_ptr<MaterialAsset> parseMaterialAsset(const VirtualPath& path,
                                                  std::string_view source,
                                                  const GuidResolver& resolver) {
    try {
        return std::make_shared<MaterialAsset>(parseMaterialAssetValue(path, source, &resolver));
    } catch (const AssetParseFailure&) {
        return {};
    } catch (const Json::exception& error) {
        Log::error("MaterialAsset", "%s: JSON value error: %s", path.string().c_str(), error.what());
        return {};
    }
}

bool validateMaterialAsset(const MaterialAsset& material,
                           const ShaderAsset& shader,
                           const VirtualPath& materialPath) {
    bool valid = true;
    const auto report = [&](const std::string& path, const std::string& message) {
        valid = false;
        Log::error("MaterialAsset",
                   "%s: %s: %s",
                   materialPath.string().c_str(),
                   path.c_str(),
                   message.c_str());
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
            report("$.keywords",
                   "keyword '" + keyword + "' is not declared by shader '" + shader.name + "'");
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

namespace {

// GUID 引用优先：resolver 认识的路径写成 guid://，否则退回绝对虚拟路径。两个
// 形态 parseMaterialAsset 都接受；SceneExport 的引用写出是同一模式。
[[nodiscard]] std::string referenceString(const VirtualPath& path, const GuidResolver& resolver) {
    if (const auto guid = resolver.findGuid(path))
        return AssetReference{*guid}.toString();
    return path.string();
}

[[nodiscard]] OrderedJson encodeShaderValue(const ShaderValue& value, const GuidResolver& resolver) {
    // 注意：nlohmann 的花括号构造（Json{scalar}）会被解释为数组；标量必须用圆括号
    // 构造或赋值。
    if (const float* scalar = std::get_if<float>(&value))
        return OrderedJson(*scalar);
    if (const bool* flag = std::get_if<bool>(&value))
        return OrderedJson(*flag);
    if (const math::Vec2* vector = std::get_if<math::Vec2>(&value))
        return OrderedJson::array({vector->x, vector->y});
    if (const math::Vec3* vector = std::get_if<math::Vec3>(&value))
        return OrderedJson::array({vector->x, vector->y, vector->z});
    if (const math::Vec4* vector = std::get_if<math::Vec4>(&value))
        return OrderedJson::array({vector->x, vector->y, vector->z, vector->w});
    const std::string& reference = std::get<std::string>(value);
    return OrderedJson(referenceString(VirtualPath{reference}, resolver));
}

} // namespace

std::string writeMaterialAssetJson(const MaterialAsset& material, const GuidResolver& resolver) {
    // 与 parseMaterialAssetValue 的读取顺序对应；可选字段（renderQueue/keywords/
    // properties）省略时保持省略，往返不把派生值显式化。
    OrderedJson root;
    root["$schemaVersion"] = 1;
    root["name"] = material.name;
    root["shader"] = referenceString(material.shader, resolver);
    if (material.renderQueue)
        root["renderQueue"] = *material.renderQueue;
    if (material.batchingMode == MaterialBatchingMode::Static)
        root["static_batch"] = true;
    if (material.batchingMode == MaterialBatchingMode::GpuInstancing)
        root["gpu_instancing"] = true;
    if (!material.keywords.empty()) {
        OrderedJson keywords = OrderedJson::array();
        for (const std::string& keyword : material.keywords)
            keywords.push_back(keyword);
        root["keywords"] = std::move(keywords);
    }
    if (!material.properties.empty()) {
        // unordered_map 遍历序不稳定；按属性名排序保证等值资产序列化为等值字节。
        std::vector<const std::pair<const std::string, ShaderValue>*> entries;
        entries.reserve(material.properties.size());
        for (const auto& entry : material.properties)
            entries.push_back(&entry);
        std::ranges::sort(entries, {}, [](const auto* entry) -> const std::string& {
            return entry->first;
        });
        OrderedJson properties = OrderedJson::object();
        for (const auto* entry : entries)
            properties[entry->first] = encodeShaderValue(entry->second, resolver);
        root["properties"] = std::move(properties);
    }
    return root.dump(2) + "\n";
}

} // namespace engine::format
