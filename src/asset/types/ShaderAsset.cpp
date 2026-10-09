#include "asset/types/ShaderAsset.h"

#include "core/logging/Log.h"
#include "core/math/hash.h"
#include "core/serialization/Transfer.h"

#include <algorithm>
#include <utility>

namespace engine {
namespace {

std::optional<ShaderPropertyType> convert(ShaderAssetPropertyType value) {
    switch (value) {
    case ShaderAssetPropertyType::Float: return ShaderPropertyType::Float;
    case ShaderAssetPropertyType::Range: return ShaderPropertyType::Float;
    case ShaderAssetPropertyType::Vec2: return ShaderPropertyType::Vec2;
    case ShaderAssetPropertyType::Vec3: return ShaderPropertyType::Vec3;
    case ShaderAssetPropertyType::Vec4: return ShaderPropertyType::Vec4;
    case ShaderAssetPropertyType::Color: return ShaderPropertyType::Vec4;
    case ShaderAssetPropertyType::Texture2D: return ShaderPropertyType::Texture2D;
    case ShaderAssetPropertyType::Boolean: return ShaderPropertyType::Boolean;
    case ShaderAssetPropertyType::Matrix: return ShaderPropertyType::Matrix;
    }
    return std::nullopt;
}

std::optional<ShaderPassType> convert(ShaderAssetPassType value) {
    switch (value) {
    case ShaderAssetPassType::Forward: return ShaderPassType::Forward;
    case ShaderAssetPassType::DepthOnly: return ShaderPassType::DepthOnly;
    case ShaderAssetPassType::ShadowCaster: return ShaderPassType::ShadowCaster;
    }
    return std::nullopt;
}

std::optional<CullMode> convert(ShaderAssetCullMode value) {
    switch (value) {
    case ShaderAssetCullMode::Off: return CullMode::Off;
    case ShaderAssetCullMode::Front: return CullMode::Front;
    case ShaderAssetCullMode::Back: return CullMode::Back;
    }
    return std::nullopt;
}

std::optional<FrontFace> convert(ShaderAssetFrontFace value) {
    switch (value) {
    case ShaderAssetFrontFace::Clockwise: return FrontFace::Clockwise;
    case ShaderAssetFrontFace::CounterClockwise: return FrontFace::CounterClockwise;
    }
    return std::nullopt;
}

std::optional<FillMode> convert(ShaderAssetFillMode value) {
    switch (value) {
    case ShaderAssetFillMode::Solid: return FillMode::Solid;
    case ShaderAssetFillMode::Wireframe: return FillMode::Wireframe;
    }
    return std::nullopt;
}

std::optional<PrimitiveTopology> convert(ShaderAssetPrimitiveTopology value) {
    switch (value) {
    case ShaderAssetPrimitiveTopology::TriangleList: return PrimitiveTopology::TriangleList;
    case ShaderAssetPrimitiveTopology::LineList: return PrimitiveTopology::LineList;
    }
    return std::nullopt;
}

std::optional<DepthCompare> convert(ShaderAssetDepthCompare value) {
    switch (value) {
    case ShaderAssetDepthCompare::Never: return DepthCompare::Never;
    case ShaderAssetDepthCompare::Less: return DepthCompare::Less;
    case ShaderAssetDepthCompare::LessEqual: return DepthCompare::LessEqual;
    case ShaderAssetDepthCompare::Equal: return DepthCompare::Equal;
    case ShaderAssetDepthCompare::Greater: return DepthCompare::Greater;
    case ShaderAssetDepthCompare::GreaterEqual: return DepthCompare::GreaterEqual;
    case ShaderAssetDepthCompare::Always: return DepthCompare::Always;
    }
    return std::nullopt;
}

std::optional<BlendMode> convert(ShaderAssetBlendMode value) {
    switch (value) {
    case ShaderAssetBlendMode::Off: return BlendMode::Off;
    case ShaderAssetBlendMode::Alpha: return BlendMode::Alpha;
    case ShaderAssetBlendMode::Additive: return BlendMode::Additive;
    case ShaderAssetBlendMode::PremultipliedAlpha: return BlendMode::PremultipliedAlpha;
    }
    return std::nullopt;
}

std::optional<ShaderValueType> convert(ShaderAssetValueType value) {
    switch (value) {
    case ShaderAssetValueType::Float: return ShaderValueType::Float;
    case ShaderAssetValueType::Vec2: return ShaderValueType::Vec2;
    case ShaderAssetValueType::Vec3: return ShaderValueType::Vec3;
    case ShaderAssetValueType::Vec4: return ShaderValueType::Vec4;
    }
    return std::nullopt;
}

std::optional<ShaderInterpolation> convert(ShaderAssetInterpolation value) {
    switch (value) {
    case ShaderAssetInterpolation::Smooth: return ShaderInterpolation::Smooth;
    case ShaderAssetInterpolation::Flat: return ShaderInterpolation::Flat;
    case ShaderAssetInterpolation::NoPerspective: return ShaderInterpolation::NoPerspective;
    }
    return std::nullopt;
}

template <typename Input, typename Output>
bool convertInterfaces(const std::vector<Input>& input, std::vector<Output>& output) {
    for (const auto& item : input) {
        const auto type = convert(item.type);
        const auto interpolation = convert(item.interpolation);
        if (!type || !interpolation)
            return false;
        output.push_back({item.name, item.semantic, *type, item.location, *interpolation});
    }
    return true;
}

std::optional<ShaderCreateInfo> convertData(const ShaderAssetData& data, ShaderCompileKey key) {
    ShaderCreateInfo info;
    info.name = data.name;
    info.compileKey = key;
    const auto properties = [](const auto& input, auto& output) {
        for (const auto& property : input) {
            const auto type = convert(property.type);
            if (!type)
                return false;
            ShaderValue value = std::visit([](const auto& item) -> ShaderValue { return item; },
                                           property.defaultValue);
            output.push_back({property.name, *type, std::move(value)});
        }
        return true;
    };
    if (!properties(data.properties, info.properties) ||
        !properties(data.globalProperties, info.globalProperties))
        return std::nullopt;
    for (const auto& sub : data.subShaders) {
        SubShaderCreateInfo runtimeSub;
        runtimeSub.renderPipeline = sub.renderPipeline;
        runtimeSub.renderQueue = sub.renderQueue;
        for (const auto& pass : sub.passes) {
            const auto type = convert(pass.pass.type);
            const auto cull = convert(pass.renderState.cull);
            const auto frontFace = convert(pass.renderState.frontFace);
            const auto fill = convert(pass.renderState.fill);
            const auto topology = convert(pass.renderState.topology);
            const auto depth = convert(pass.renderState.depthTest);
            const auto blend = convert(pass.renderState.blend);
            if (!type || !cull || !frontFace || !fill || !topology || !depth || !blend)
                return std::nullopt;
            ShaderPassCreateInfo runtimePass;
            runtimePass.name = pass.pass.name;
            runtimePass.type = *type;
            runtimePass.program = {pass.pass.program.vertexSource,
                                   pass.pass.program.fragmentSource};
            runtimePass.features = pass.pass.features;
            if (!convertInterfaces(pass.pass.vertexInput, runtimePass.vertexInput) ||
                !convertInterfaces(pass.pass.varyings, runtimePass.varyings) ||
                !convertInterfaces(pass.pass.fragmentOutputs, runtimePass.fragmentOutputs))
                return std::nullopt;
            runtimePass.renderState.cull = *cull;
            runtimePass.renderState.frontFace = *frontFace;
            runtimePass.renderState.fill = *fill;
            runtimePass.renderState.topology = *topology;
            runtimePass.renderState.depthWrite = pass.renderState.depthWrite;
            runtimePass.renderState.depthTest = *depth;
            runtimePass.renderState.blend = *blend;
            runtimePass.renderState.colorMask = pass.renderState.colorMask;
            runtimeSub.passes.push_back(std::move(runtimePass));
        }
        info.subShaders.push_back(std::move(runtimeSub));
    }
    if (!validateShaderCreateInfo(info))
        return std::nullopt;
    return info;
}

} // namespace

std::optional<ShaderCreateInfo> ShaderAsset::buildRuntimeInfo() const {
    return convertData(data, hashString(assetPath().string()));
}

Ref<Shader> ShaderAsset::instantiate() {
    if (!instance_) {
        auto info = buildRuntimeInfo();
        if (!info)
            return {};
        instance_ = makeRef<Shader>(std::move(*info));
    }
    return instance_;
}

Ref<Shader> ShaderAsset::clone() const {
    auto info = buildRuntimeInfo();
    return info ? makeRef<Shader>(std::move(*info)) : Ref<Shader>{};
}

bool ShaderAsset::transfer(Transfer& archive) {
    if (archive.writing())
        return data.transfer(archive);
    ShaderAssetData decoded;
    if (!decoded.transfer(archive))
        return false;
    auto info = convertData(decoded, hashString(assetPath().string()));
    if (!info || (instance_ && !instance_->rebuild(std::move(*info)))) {
        Log::error("ShaderAsset", "Invalid runtime payload: %s", assetPath().string().c_str());
        return false;
    }
    data = std::move(decoded);
    return true;
}

const ShaderPropertyAssetData* ShaderAsset::findProperty(const std::string& name) const {
    const auto it = std::ranges::find(data.properties, name, &ShaderPropertyAssetData::name);
    return it == data.properties.end() ? nullptr : &*it;
}

const ShaderPropertyAssetData* ShaderAsset::findGlobalProperty(const std::string& name) const {
    const auto it = std::ranges::find(data.globalProperties, name, &ShaderPropertyAssetData::name);
    return it == data.globalProperties.end() ? nullptr : &*it;
}

} // namespace engine
